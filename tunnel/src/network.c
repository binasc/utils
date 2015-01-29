#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include "network.h"
#include "log.h"

int nl_queryname(const char *name, struct in_addr *addr)
{
    int rc;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    //hints.ai_family = AF_UNSPEC;  /* Allow IPv4 or IPv6 */
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(name, NULL, &hints, &result);
    if (rc == -1) {
        log_error("getaddrinfo: %s", gai_strerror(rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_addrlen == sizeof(struct sockaddr_in)) {
            *addr = ((struct sockaddr_in *)rp->ai_addr)->sin_addr;
            freeaddrinfo(result);
            return 0;
        }
    }

    freeaddrinfo(result);
    log_error("cann't resolve %s", name);
    return -1;
}

static nl_connection_t *nl_connection_create()
{
    nl_connection_t *c;

    c = calloc(1, sizeof(nl_connection_t));
    if (c == NULL) {
        return NULL;
    }

    c->tosend = list_create(sizeof(nl_buf_t), NULL, NULL);
    if (c->tosend == NULL) {
        free(c);
        return NULL;
    }

    return c;
}

/* wrapper */
static void accept_handler(nl_event_t *ev);
static void connect_handler(nl_event_t *ev);
static void read_handler(nl_event_t *ev);
static void write_handler(nl_event_t *ev);

static void accept_handler(nl_event_t *ev)
{
    nl_socket_t         *sock;
    nl_connection_t     *c, *nc;
    int                 rc;

    sock = ev->data;
    log_trace("#%d accept_handler", sock->fd);
    c = sock->data;

    c->error = 0;
    for ( ; ; ) {
        nc = nl_connection_create();
        if (nc == NULL) {
            break;
        }

        rc = nl_accept(sock, &nc->sock);
        if (rc == -1) {
            if (sock->error) {
                c->error = 1;
                nl_connection_close(c);
            }
            else if (nc->sock.error) {
                nc->error = 1;
                nl_connection_close(nc);
            }
            break;
        }

        nc->sock.data = nc;
        nc->sock.wev.handler = write_handler;
        nc->sock.rev.handler = read_handler;
        c->cbs.on_accepted(c, nc);
    }
}

static void connect_handler(nl_event_t *ev)
{
    nl_socket_t *sock;
    nl_connection_t *c;

    sock = ev->data;
    log_trace("#%d connect_handler", sock->fd);
    c = sock->data;

    if (!sock->connected || sock->error) {
        c->error = 1;
        nl_connection_close(c);
        return;
    }

    if (c->cbs.on_connected) {
        c->cbs.on_connected(c);
    }
    else {
        nl_event_add(&sock->rev);
    }

    sock->rev.handler = read_handler;
    sock->wev.handler = write_handler;
    if (list_empty(c->tosend)) {
        nl_event_del(&sock->wev);
    }
}

static int enlarge_buffer(nl_connection_t *c, size_t size)
{
    char *buf = NULL;

    if (size > c->remain_size) {
        buf = realloc(c->remain.buf, size);
        if (buf == NULL) {
            free(c->remain.buf);
            c->remain.buf = NULL;
            c->remain.len = 0;
            c->remain_size = 0;
            return -1;
        }
        else {
            c->remain.buf = buf;
            c->remain_size = size;
            return 0;
        }
    }

    return 0;
}

static int packet_handler(nl_connection_t *c, char *buf, size_t len)
{
    nl_buf_t    in, out;
    int         n;

    if (c->remain.len == 0) {
        in.buf = buf;
        in.len = len;
    }
    else {
        if (enlarge_buffer(c, c->remain.len + len) < 0) {
            return -1;
        }
        memcpy(c->remain.buf + c->remain.len, buf, len);
        c->remain.len += len;
        in = c->remain;
    }

    if (c->cbs.splitter != NULL) {
        while ((n = c->cbs.splitter(c, &in, &out)) > 0) {
            in.buf += n;
            in.len -= n;
            if (out.len > 0) {
                c->cbs.on_received(c, &out);
            }
        }
        if (n < 0) {
            return -1;
        }
    }
    else {
        out = in;
        in.buf += in.len;
        in.len = 0;
        c->cbs.on_received(c, &out);
    }

    if (in.len > 0) {
        if (enlarge_buffer(c, in.len) < 0) {
            return -1;
        }
        memmove(c->remain.buf, in.buf, in.len);
    }
    c->remain.len = in.len;

    return 0;
}

static void read_handler(nl_event_t *ev)
{
// TODO: multi-threads
#define RECV_BUFF_SIZE 16384
    static char s_recv_buff[RECV_BUFF_SIZE];

    int rc;
    nl_socket_t *sock;
    nl_connection_t *c;

    sock = ev->data;
    log_trace("#%d read_handler", sock->fd);
    c = sock->data;
    c->error = 0;

    for ( ; ; ) {
        rc = nl_recv(sock, s_recv_buff, RECV_BUFF_SIZE);
        if (rc <= 0) {
            if (rc == -1 && !sock->error) {
                /* EAGAIN || EWOULDBLOCK */
                return;
            }
            else if (rc != 0) {
                c->error = 1;
            }
            break;
        }
        else {
            rc = packet_handler(sock->data, s_recv_buff, rc);
            if (rc < 0) {
                c->error = 1;
                break;
            }
            else if (!sock->rev.active) {
                return;
            }
        }
    }

    nl_connection_close(c);
}

static void nl_connection_destroy(nl_connection_t *c)
{
    struct list_iterator_t  it;
    nl_buf_t                *buf;

    for (it = list_begin(c->tosend);
         !list_iterator_equal(list_end(c->tosend), it);
         it = list_iterator_next(it)) {
        buf = list_iterator_item(it);
        free(buf->buf);
    }
    list_destroy(c->tosend);
    if (c->remain.buf != NULL) {
        free(c->remain.buf);
        c->remain.buf = NULL;
        c->remain.len = 0;
        c->remain_size = 0;
    }

    log_debug("#%d destroyed", c->sock.fd);

    nl_close(&c->sock);

    if (c->cbs.on_closed != NULL) {
        c->cbs.on_closed(c);
    }

    free(c);
}

static void linger_handler(nl_event_t *ev)
{
    log_trace("linger_handler");
    nl_connection_destroy((nl_connection_t *)ev->data);
}

static void write_handler(nl_event_t *ev)
{
    nl_socket_t         *sock;
    nl_connection_t     *c;
    nl_buf_t            *buf, snd;
    int                 rc;

    sock = ev->data;
    log_trace("#%d write_handler", sock->fd);
    c = sock->data;
    while (!list_empty(c->tosend)) {
        buf = (nl_buf_t *)list_front(c->tosend);
        rc = nl_send(sock, buf->buf, buf->len);
        snd.buf = buf->buf;
        snd.len = rc;
        if (rc == (int)buf->len) {
            if (c->cbs.on_sent) {
                c->cbs.on_sent(c, &snd);
            }
            free(buf->buf);
            list_pop_front(c->tosend);
        }
        else if (rc > 0 && rc < buf->len) {
            if (c->cbs.on_sent) {
                c->cbs.on_sent(c, &snd);
            }
            memmove(buf->buf, buf->buf + rc, buf->len - rc);
            buf->len -= rc;
        }
        else {
            if (rc == -1 && !sock->error) {
                //nl_event_add(&sock->wev);
            }
            else {
                c->error = 1;
                if (c->closing_ev.timer_set) {
                    nl_event_del_timer(&c->closing_ev);
                }
                nl_connection_close(c);
                return;
            }
        }
    }

    if (list_empty(c->tosend)) {
        nl_event_del(&c->sock.wev);
        if (c->closing_ev.timer_set) {
            nl_event_del_timer(&c->closing_ev);
            nl_connection_close(c);
        }
    }
}

nl_connection_t *nl_connection()
{
    int                 rc;
    nl_connection_t    *c;

    c = nl_connection_create();
    if (c == NULL) {
        return NULL;
    }

    rc = nl_socket(&c->sock, NL_TCP);
    if (rc == -1) {
        c->error = 1;
        nl_connection_close(c);
        return NULL;
    }

    c->sock.data = c;

    return c;
}

int nl_connection_listen(nl_connection_t *c, struct sockaddr_in *addr, int backlog)
{
    c->sock.rev.handler = accept_handler;
    c->sock.wev.handler = NULL;
    if (nl_bind(&c->sock, addr) < 0) {
        return -1;
    }
    return nl_listen(&c->sock, backlog);
}

int nl_connection_connect(nl_connection_t *c, struct sockaddr_in *addr)
{
    c->sock.rev.handler = NULL;
    c->sock.wev.handler = connect_handler;
    return nl_connect(&c->sock, addr);
}

int nl_connection_send(nl_connection_t *c, nl_buf_t *buf)
{
    nl_buf_t tosend;

    if (c->closing_ev.timer_set) {
        return -1;
    }

    tosend.buf = malloc(buf->len);
    if (tosend.buf == NULL) {
        return -1;
    }
    memcpy(tosend.buf, buf->buf, buf->len);
    tosend.len = buf->len;

    if (c->sock.connected && list_empty(c->tosend)) {
        nl_event_add(&c->sock.wev);
    }

    list_push_back(c->tosend, &tosend);

    return 0;
}

int nl_connection_close(nl_connection_t *c)
{
    int timeout;

    if (c->closing_ev.timer_set) {
        return 0;
    }

    timeout = 0;
    if (!c->error && c->sock.connected && !list_empty(c->tosend) /* && linger */) {
        timeout = 20000;
    }

    if (timeout == 0) {
        nl_event_del(&c->sock.wev);
        log_debug("#%d closing in next loop", c->sock.fd);
    }
    else {
        log_debug("#%d closing in %d ms", c->sock.fd, timeout);
    }

    nl_event_del(&c->sock.rev);
    c->closing_ev.handler = linger_handler;
    c->closing_ev.data = c;
    nl_event_add_timer(&c->closing_ev, timeout);

    return 0;
}

void nl_connection_pause_receiving(nl_connection_t *c)
{
    nl_event_del(&c->sock.rev);
}

void nl_connection_resume_receiving(nl_connection_t *c)
{
    if (!c->closing_ev.timer_set && !c->sock.rev.active) {
        nl_event_add(&c->sock.rev);
    }
}

void nl_connection_pause_sending(nl_connection_t *c)
{
    nl_event_del(&c->sock.wev);
}

void nl_connection_resume_sending(nl_connection_t *c)
{
    nl_event_add(&c->sock.wev);
}

static void nl_datagram_destroy(nl_datagram_t *c)
{
    struct list_iterator_t  it;
    nl_packet_t             *p;

    for (it = list_begin(c->tosend);
         !list_iterator_equal(list_end(c->tosend), it);
         it = list_iterator_next(it)) {
        p = list_iterator_item(it);
        free(p->buf.buf);
    }
    list_destroy(c->tosend);

    log_debug("#%d destroyed", c->sock.fd);

    nl_close(&c->sock);

    if (c->on_closed) {
        c->on_closed(c);
    }
}

static void udp_linger_handler(nl_event_t *ev)
{
    log_trace("udp_linger_handler");
    nl_datagram_destroy((nl_datagram_t *)ev->data);
}

static void udp_read_handler(nl_event_t *ev)
{
// TODO: multi-threads
#define RECV_BUFF_SIZE 16384
    static char s_recv_buff[RECV_BUFF_SIZE];

    int             rc;
    nl_socket_t    *sock;
    nl_datagram_t  *d;
    nl_packet_t     p;

    sock = ev->data;
    log_trace("#%d udp_read_handler", sock->fd);
    d = sock->data;
    d->error = 0;

    for ( ; ; ) {
        rc = nl_recvfrom(sock, s_recv_buff, RECV_BUFF_SIZE, &p.addr);
        if (rc < 0) {
            if (!sock->error) {
                /* EAGAIN || EWOULDBLOCK */
                return;
            }
            d->error = 1;
            break;
        }
        else {
            p.buf.buf = s_recv_buff;
            p.buf.len = rc;
            d->on_received(d, &p);
            if (!sock->rev.active) {
                return;
            }
        }
    }

    nl_datagram_close(d);
}

static void udp_write_handler(nl_event_t *ev)
{
    nl_socket_t         *sock;
    nl_datagram_t       *d;
    nl_packet_t         *p;
    nl_buf_t            *buf;
    int                 rc;

    sock = ev->data;
    log_trace("#%d udpwrite_handler", sock->fd);
    d = sock->data;
    while (!list_empty(d->tosend)) {
        p = (nl_packet_t *)list_front(d->tosend);
        buf = &p->buf;
        rc = nl_sendto(sock, buf->buf, buf->len, &p->addr);
        if (rc == (int)buf->len) {
            if (d->on_sent) {
                d->on_sent(d, p);
            }
            free(buf->buf);
            list_pop_front(d->tosend);
        }
        else if (rc > 0 && rc < buf->len) {
            log_fatal("?????????????????????");
            if (d->on_sent) {
                d->on_sent(d, p);
            }
            memmove(buf->buf, buf->buf + rc, buf->len - rc);
            buf->len -= rc;
        }
        else {
            if (rc == -1 && !sock->error) {
                //nl_event_add(&sock->wev);
            }
            else {
                d->error = 1;
                if (d->closing_ev.timer_set) {
                    nl_event_del_timer(&d->closing_ev);
                }
                nl_datagram_close(d);
                return;
            }
        }
    }

    if (list_empty(d->tosend)) {
        nl_event_del(&d->sock.wev);
        if (d->closing_ev.timer_set) {
            nl_event_del_timer(&d->closing_ev);
            nl_datagram_close(d);
        }
    }
}

int nl_datagram(nl_datagram_t *d)
{
    int rc;

    memset(d, 0, sizeof(nl_datagram_t));

    d->tosend = list_create(sizeof(nl_packet_t), NULL, NULL);
    if (d->tosend == NULL) {
        return -1;
    }

    rc = nl_socket(&d->sock, NL_UDP);
    if (rc == -1) {
        d->error = 1;
        nl_datagram_close(d);
        return -1;
    }

    d->sock.data = d;
    d->sock.rev.handler = udp_read_handler;
    d->sock.wev.handler = udp_write_handler;

    return 0;
}

int nl_datagram_bind(nl_datagram_t *d, struct sockaddr_in *addr)
{
    return nl_bind(&d->sock, addr);
}

int nl_datagram_send(nl_datagram_t *d, nl_packet_t *p)
{
    nl_packet_t tosend;

    if (d->closing_ev.timer_set) {
        return -1;
    }

    tosend.buf.buf = malloc(p->buf.len);
    if (tosend.buf.buf == NULL) {
        return -1;
    }
    memcpy(&tosend.addr, &p->addr, sizeof(tosend.addr));
    memcpy(tosend.buf.buf, p->buf.buf, p->buf.len);
    tosend.buf.len = p->buf.len;

    if (list_empty(d->tosend)) {
        nl_event_add(&d->sock.wev);
    }

    list_push_back(d->tosend, &tosend);

    return 0;
}

int nl_datagram_close(nl_datagram_t *d)
{
    if (d->closing_ev.timer_set) {
        return 0;
    }

    nl_event_del(&d->sock.wev);
    nl_event_del(&d->sock.rev);
    d->closing_ev.handler = udp_linger_handler;
    d->closing_ev.data = d;
    nl_event_add_timer(&d->closing_ev, 0);

    return 0;
}


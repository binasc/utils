#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include "stream.h"
#include "log.h"

/* wrapper */
static void accept_handler(nl_event_t *ev);
static void connect_handler(nl_event_t *ev);
static void read_handler(nl_event_t *ev);
static void write_handler(nl_event_t *ev);

static void accept_handler(nl_event_t *ev)
{
    nl_socket_t         *sock;
    nl_stream_t         *c;
    int                 rc;

    sock = ev->data;
    log_trace("#%d accept_handler", sock->fd);
    c = sock->data;

    c->error = 0;
    for ( ; ; ) {
        rc = nl_accept(sock, &c->accepted);
        if (rc == -1) {
            if (sock->error) {
                c->error = 1;
                nl_stream_close(c);
                break;
            }
            else if (c->accepted.error) {
            }
        }

        c->cbs.on_accepted(c);
    }
}

static void connect_handler(nl_event_t *ev)
{
    nl_socket_t *sock;
    nl_stream_t *c;

    sock = ev->data;
    log_trace("#%d connect_handler", sock->fd);
    c = sock->data;

    if (!sock->connected || sock->error) {
        c->error = 1;
        nl_stream_close(c);
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

static int enlarge_buffer(nl_stream_t *c, size_t size)
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

static int packet_handler(nl_stream_t *c, char *buf, size_t len)
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
    nl_stream_t *c;

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

    nl_stream_close(c);
}

static void nl_stream_destroy(nl_stream_t *c)
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
}

static void linger_handler(nl_event_t *ev)
{
    log_trace("linger_handler");
    nl_stream_destroy((nl_stream_t *)ev->data);
}

static void write_handler(nl_event_t *ev)
{
    nl_socket_t         *sock;
    nl_stream_t     *c;
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
                nl_stream_close(c);
                return;
            }
        }
    }

    if (list_empty(c->tosend)) {
        nl_event_del(&c->sock.wev);
        if (c->closing_ev.timer_set) {
            nl_event_del_timer(&c->closing_ev);
            nl_stream_close(c);
        }
    }
}

int nl_stream(nl_stream_t *s)
{
    int rc;

    memset(s, 0, sizeof(nl_stream_t));

    s->tosend = list_create(sizeof(nl_buf_t), NULL, NULL);
    if (s->tosend == NULL) {
        return -1;
    }

    rc = nl_socket(&s->sock, NL_TCP);
    if (rc == -1) {
        s->error = 1;
        nl_stream_close(s);
        return -1;
    }

    s->sock.data = s;

    return 0;
}

int nl_stream_listen(nl_stream_t *c, nl_address_t *addr, int backlog)
{
    c->sock.rev.handler = accept_handler;
    c->sock.wev.handler = NULL;
    if (nl_bind(&c->sock, addr) < 0) {
        return -1;
    }
    return nl_listen(&c->sock, backlog);
}

int nl_stream_accept(nl_stream_t *acceptor, nl_stream_t *s)
{
    memset(s, 0, sizeof(nl_stream_t));

    s->tosend = list_create(sizeof(nl_buf_t), NULL, NULL);
    if (s->tosend == NULL) {
        return -1;
    }

    nl_socket_copy(&s->sock, &acceptor->accepted);

    s->sock.data = s;

    s->sock.wev.handler = write_handler;
    s->sock.rev.handler = read_handler;

    return 0;
}

int nl_stream_connect(nl_stream_t *c, nl_address_t *addr)
{
    c->sock.rev.handler = NULL;
    c->sock.wev.handler = connect_handler;
    return nl_connect(&c->sock, addr);
}

int nl_stream_send(nl_stream_t *c, nl_buf_t *buf)
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

int nl_stream_close(nl_stream_t *c)
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

void nl_stream_pause_receiving(nl_stream_t *c)
{
    nl_event_del(&c->sock.rev);
}

void nl_stream_resume_receiving(nl_stream_t *c)
{
    if (!c->closing_ev.timer_set && !c->sock.rev.active) {
        nl_event_add(&c->sock.rev);
    }
}

void nl_stream_pause_sending(nl_stream_t *c)
{
    nl_event_del(&c->sock.wev);
}

void nl_stream_resume_sending(nl_stream_t *c)
{
    nl_event_add(&c->sock.wev);
}


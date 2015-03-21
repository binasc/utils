#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

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
                // nothing to do
            }
            else {
                break;
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

static int enlarge_buffer(nl_decoder_t *d, size_t size)
{
    char *buf = NULL;

    if (size > d->remain_size) {
        buf = realloc(d->remain.buf, size);
        if (buf == NULL) {
            free(d->remain.buf);
            d->remain.buf = NULL;
            d->remain.len = 0;
            d->remain_size = 0;
            return -1;
        }
        else {
            d->remain.buf = buf;
            d->remain_size = size;
            return 0;
        }
    }

    return 0;
}

static int is_decoder_valid(nl_stream_t *s, int pos)
{
    int depth = 0;
    nl_decoder_t *d;

    d = s->decoders;
    while (d) {
        d = d->next;
        depth++;
    }

    return pos < depth ? 1 : 0;
}

static int decode(nl_stream_t *s, nl_decoder_t *d, int depth, char *buf, size_t len)
{
    nl_buf_t        in, out;
    int             n;

    log_trace("#%d decode", s->sock.fd);

    if (d->remain.len == 0) {
        in.buf = buf;
        in.len = len;
    }
    else {
        if (enlarge_buffer(d, d->remain.len + len) < 0) {
            return -1;
        }
        memcpy(d->remain.buf + d->remain.len, buf, len);
        d->remain.len += len;
        in = d->remain;
    }

    while ((n = d->decoder(d->data, &in, &out)) > 0) {
        in.buf += n;
        in.len -= n;
        if (out.len > 0) {
            if (d->next == NULL) {
                s->cbs.on_received(s, &out);
            }
            else {
                if (decode(s, d->next, depth + 1, out.buf, out.len) < 0) {
                    return -1;
                }
            }
        }
        if (!is_decoder_valid(s, depth)) {
            out = in;
            s->cbs.on_received(s, &out);
            return 0;
        }
    }
    if (n < 0) {
        return -1;
    }

    if (in.len > 0) {
        if (enlarge_buffer(d, in.len) < 0) {
            return -1;
        }
        memmove(d->remain.buf, in.buf, in.len);
    }
    d->remain.len = in.len;

    return 0;
}

static int packet_handler(nl_stream_t *s, char *buf, size_t len)
{
    if (s->decoders != NULL) {
        if (decode(s, s->decoders, 0, buf, len) < 0) {
            return -1;
        }
    }
    else {
        nl_buf_t b = { buf, len };
        s->cbs.on_received(s, &b);
    }

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

static void nl_stream_destroy(nl_stream_t *s)
{
    struct list_iterator_t  it;
    nl_buf_t                *buf;

    for (it = list_begin(s->tosend);
         !list_iterator_equal(list_end(s->tosend), it);
         it = list_iterator_next(it)) {
        buf = list_iterator_item(it);
        free(buf->buf);
    }
    list_destroy(s->tosend);

    while (s->encoders != NULL) {
        nl_stream_encoder_pop_back(s);
    }
    while (s->decoders != NULL) {
        nl_stream_decoder_pop_back(s);
    }

    log_debug("#%d destroyed", s->sock.fd);

    nl_close(&s->sock);

    if (s->cbs.on_closed != NULL) {
        s->cbs.on_closed(s);
    }
}

static void linger_handler(nl_event_t *ev)
{
    log_trace("#- linger_handler");
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
            /* accurate pending bytes */
            list_pop_front(c->tosend);
            if (c->cbs.on_sent) {
                c->cbs.on_sent(c, &snd);
            }
            free(buf->buf);
        }
        else if (rc > 0 && rc < buf->len) {
            /* accurate pending bytes */
            buf->len -= rc;
            if (c->cbs.on_sent) {
                c->cbs.on_sent(c, &snd);
            }
            //memmove(buf->buf, buf->buf + rc, buf->len - rc);
            memmove(buf->buf, buf->buf + rc, buf->len);
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

    rc = nl_socket(&s->sock, NL_STREAM);
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

int nl_stream_send(nl_stream_t *s, nl_buf_t *b)
{
    char *buf;
    size_t len;
    nl_encoder_t *e;
    nl_buf_t tosend;

    if (s->closing_ev.timer_set) {
        return -1;
    }

    buf = b->buf;
    len = b->len;
    for (e = s->encoders; e != NULL; e = e->next) {
        buf = e->encoder(e->data, buf, &len);
        if (buf == NULL) {
            return -1;
        }
    }

    tosend.buf = malloc(len);
    if (tosend.buf == NULL) {
        return -1;
    }
    memcpy(tosend.buf, buf, len);
    tosend.len = len;

    if (s->sock.connected && list_empty(s->tosend)) {
        nl_event_add(&s->sock.wev);
    }

    list_push_back(s->tosend, &tosend);

    return len;
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

int nl_stream_encoder_push_back(nl_stream_t *s, nl_encoder_fn enc, void *data)
{
    nl_encoder_t **pe, *ne;

    ne = calloc(1, sizeof(nl_encoder_t));
    if (ne == NULL) {
        log_error("#%d malloc failed", s->sock.fd);
        return -1;
    }

    pe = &s->encoders;
    while (*pe) {
        pe = &(*pe)->next;
    }

    ne->encoder = enc;
    ne->data = data;
    *pe = ne;

    return 0;
}

int nl_stream_encoder_pop_back(nl_stream_t *s)
{
    nl_encoder_t **pe;

    if (s->encoders == NULL) {
        return -1;
    }

    pe = &s->encoders;
    while ((*pe)->next) {
        pe = &(*pe)->next;
    }

    free(*pe);
    *pe = NULL;

    return 0;
}

int nl_stream_decoder_push_back(nl_stream_t *s, nl_decoder_fn dec, void *data)
{
    nl_decoder_t **pd, *nd;

    nd = calloc(1, sizeof(nl_decoder_t));
    if (nd == NULL) {
        log_error("#%d malloc failed", s->sock.fd);
        return -1;
    }

    pd = &s->decoders;
    while (*pd) {
        pd = &(*pd)->next;
    }

    nd->decoder = dec;
    nd->data = data;
    *pd = nd;

    return 0;
}

int nl_stream_decoder_pop_back(nl_stream_t *s)
{
    nl_decoder_t **pd, *d;

    if (s->decoders == NULL) {
        return -1;
    }

    pd = &s->decoders;
    while ((*pd)->next) {
        pd = &(*pd)->next;
    }

    d = *pd;
    if (d->remain.buf != NULL) {
        free(d->remain.buf);
        d->remain.buf = NULL;
        d->remain.len = 0;
        d->remain_size = 0;
    }
    free(d);
    *pd = NULL;

    return 0;
}

size_t nl_stream_pending_bytes(nl_stream_t *s)
{
    size_t len;
    nl_buf_t *buf;
    struct list_iterator_t  it;

    len = 0;
    for (it = list_begin(s->tosend);
         !list_iterator_equal(list_end(s->tosend), it);
         it = list_iterator_next(it)) {
        buf = list_iterator_item(it);
        len += buf->len;
    }

    return len;
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


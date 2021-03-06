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
    nl_stream_t         *s;
    int                 rc;

    sock = ev->data;
    log_trace("#%d accept_handler", sock->fd);
    s = sock->data;

    s->error = 0;
    for ( ; ; ) {
        rc = nl_accept(sock, &s->accepted_sock);
        if (rc == -1) {
            if (sock->error) {
                s->error = 1;
                nl_stream_close(s);
                break;
            }
            else if (s->accepted_sock.error) {
                continue;
            }
            else {
                break;
            }
        }

        s->accepted = 0;
        s->cbs.on_accepted(s);
        if (!s->accepted) {
            nl_close(&s->accepted_sock);
        }
    }
}

static void connect_handler(nl_event_t *ev)
{
    nl_socket_t *sock;
    nl_stream_t *s;

    sock = ev->data;
    log_trace("#%d connect_handler", sock->fd);
    s = sock->data;

    if (!sock->connected || sock->error) {
        s->error = 1;
        nl_stream_close(s);
        return;
    }

    if (s->cbs.on_connected) {
        s->cbs.on_connected(s);
    }
    else {
        nl_event_add(&sock->rev);
    }

    sock->rev.handler = read_handler;
    sock->wev.handler = write_handler;
    if (list_empty(s->tosend)) {
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
                log_trace("#%d decode %zu bytes", s->sock.fd, out.len);
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
            if (out.len > 0) {
                log_trace("#%d decode remain %zu bytes", s->sock.fd, out.len);
                s->cbs.on_received(s, &out);
            }
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
    log_trace("#%d packet_handler", s->sock.fd);

    if (s->decoders != NULL) {
        if (decode(s, s->decoders, 0, buf, len) < 0) {
            return -1;
        }
    }
    else {
        log_trace("#%d direct %zu bytes", s->sock.fd, len);
        nl_buf_t b = { buf, len };
        s->cbs.on_received(s, &b);
    }

    return 0;
}

#ifndef RECV_BUFF_SIZE
#define RECV_BUFF_SIZE 16384
#endif

static void read_handler(nl_event_t *ev)
{
    static char s_recv_buff[RECV_BUFF_SIZE];

    int rc;
    nl_socket_t *sock;
    nl_stream_t *s;

    sock = ev->data;
    log_trace("#%d read_handler", sock->fd);
    s = sock->data;

    s->error = 0;
    for ( ; ; ) {
        rc = nl_recv(sock, s_recv_buff, RECV_BUFF_SIZE);
        if (rc < 0) {
            if (!sock->error) {
                /* EAGAIN || EWOULDBLOCK */
                return;
            }
            s->error = 1;
            break;
        }
        else if (rc == 0) {
            break;
        }
        else {
            rc = packet_handler(s, s_recv_buff, rc);
            if (rc < 0) {
                s->error = 1;
                break;
            }
            else if (!sock->rev.active) {
                return;
            }
        }
    }

    nl_stream_close(s);
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

    log_trace("#%d destroyed", s->sock.fd);

    nl_close(&s->sock);

    if (s->cbs.on_closed != NULL) {
        s->cbs.on_closed(s);
    }
}

static void linger_handler(nl_event_t *ev)
{
    nl_stream_t *s = ev->data;
    log_trace("#%d linger_handler", s->sock.fd);
    nl_stream_destroy(s);
}

static void write_handler(nl_event_t *ev)
{
    nl_socket_t         *sock;
    nl_stream_t         *s;
    nl_buf_t            *buf;
    int                 rc;

    sock = ev->data;
    log_trace("#%d write_handler", sock->fd);
    s = sock->data;

    while (!list_empty(s->tosend)) {
        buf = (nl_buf_t *)list_front(s->tosend);
        rc = nl_send(sock, buf->buf, buf->len);
        if (rc >= 0) {
            if ((size_t)rc < buf->len) {
                /* accurate pending bytes */
                buf->len -= rc;
                memmove(buf->buf, buf->buf + rc, buf->len);
                if (s->cbs.on_sent) {
                    s->cbs.on_sent(s, rc);
                }
            }
            else {
                /* accurate pending bytes */
                list_pop_front(s->tosend);
                if (s->cbs.on_sent) {
                    s->cbs.on_sent(s, rc);
                }
                free(buf->buf);
            }
        }
        else { /* rc < 0 */
            if (!sock->error) {
                return;
            }
            else {
                s->error = 1;
                if (s->closing_ev.timer_set) {
                    nl_event_del_timer(&s->closing_ev);
                }
                nl_stream_close(s);
                return;
            }
        }
    }

    /* tosend is empty */
    nl_event_del(&s->sock.wev);
    if (s->closing_ev.timer_set) {
        nl_event_del_timer(&s->closing_ev);
        nl_stream_close(s);
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
        list_destroy(s->tosend);
        s->tosend = NULL;
        s->error = 1;
        return -1;
    }

    s->id = s->sock.fd;
    s->sock.data = s;

    return 0;
}

int nl_stream_listen(nl_stream_t *s, nl_address_t *addr, int backlog)
{
    if (s->closed) {
        log_warning("#%d already closed", s->sock.fd);
        return -1;
    }

    s->sock.rev.handler = accept_handler;
    s->sock.wev.handler = NULL;
    if (nl_bind(&s->sock, addr) < 0) {
        return -1;
    }
    return nl_listen(&s->sock, backlog);
}

int nl_stream_accept(nl_stream_t *acceptor, nl_stream_t *s)
{
    if (acceptor->closed) {
        log_warning("#%d already closed", s->sock.fd);
        return -1;
    }

    memset(s, 0, sizeof(nl_stream_t));

    s->tosend = list_create(sizeof(nl_buf_t), NULL, NULL);
    if (s->tosend == NULL) {
        return -1;
    }

    nl_socket_copy(&s->sock, &acceptor->accepted_sock);

    s->sock.data = s;
    s->id = s->sock.fd;

    s->sock.wev.handler = write_handler;
    s->sock.rev.handler = read_handler;

    acceptor->accepted = 1;

    return 0;
}

int nl_stream_connect(nl_stream_t *s, nl_address_t *addr)
{
    if (s->closed) {
        log_warning("#%d already closed", s->sock.fd);
        return -1;
    }

    s->sock.rev.handler = NULL;
    s->sock.wev.handler = connect_handler;
    return nl_connect(&s->sock, addr);
}

int nl_stream_send(nl_stream_t *s, nl_buf_t *b)
{
    char *buf;
    size_t len;
    nl_encoder_t *e;
    nl_buf_t tosend;

    if (s->closed) {
        log_warning("#%d already closed", s->sock.fd);
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

int nl_stream_close(nl_stream_t *s)
{
    int timeout;

    if (s->closing_ev.timer_set) {
        return 0;
    }
    s->closed = 1;

    timeout = 0;
    if (!s->error && s->sock.connected && !list_empty(s->tosend)) {
        timeout = 20000;
    }

    if (timeout == 0) {
        nl_event_del(&s->sock.wev);
        log_debug("#%d closing in next loop", s->sock.fd);
    }
    else {
        log_debug("#%d closing in %d ms", s->sock.fd, timeout);
    }

    nl_event_del(&s->sock.rev);
    s->closing_ev.handler = linger_handler;
    s->closing_ev.data = s;
    nl_event_add_timer(&s->closing_ev, timeout);

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
    size_t                  len;
    nl_buf_t                *buf;
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

void nl_stream_pause_receiving(nl_stream_t *s)
{
    nl_event_del(&s->sock.rev);
}

void nl_stream_resume_receiving(nl_stream_t *s)
{
    if (!s->closing_ev.timer_set && !s->sock.rev.active) {
        nl_event_add(&s->sock.rev);
    }
}

void nl_stream_pause_sending(nl_stream_t *s)
{
    nl_event_del(&s->sock.wev);
}

void nl_stream_resume_sending(nl_stream_t *s)
{
    nl_event_add(&s->sock.wev);
}

int nl_stream_getsockname(nl_stream_t *s, nl_address_t *addr)
{
    return nl_socket_getsockname(&s->sock, addr);
}

int nl_stream_getpeername(nl_stream_t *s, nl_address_t *addr)
{
    return nl_socket_getpeername(&s->sock, addr);
}


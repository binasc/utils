#include "config.h"
#include "tunnel.h"
#include "obscure.h"
#include "log.h"

#include <string.h>

#define SEND_UPPER_BOUND (8*1024)
#define SEND_LOWER_BOUND (4*1024)

static void on_received(nl_stream_t *c, nl_buf_t *buf);
static void on_connected(nl_stream_t *c);
static void on_sent(nl_stream_t *c, nl_buf_t *buf);
static void on_closed(nl_stream_t *c);

static stream_tunnel_t *stream_tunnel_create()
{
    stream_tunnel_t *t;

    t = calloc(1, sizeof(stream_tunnel_t));
    if (t == NULL) {
        return NULL;
    }

    obscure_create(&t->o);

    return t;
}

static int front_splitter(void *data, const nl_buf_t *in, nl_buf_t *out)
{
    uint8_t name_len;

    if (in->len < sizeof(uint16_t) + sizeof(name_len)) {
        return 0;
    }

    name_len = *(uint8_t *)(in->buf + sizeof(uint16_t));
    if (in->len < sizeof(uint16_t) + sizeof(name_len) + name_len) {
        return 0;
    }

    out->buf = in->buf;
    out->len = sizeof(uint16_t) + sizeof(name_len) + name_len;

    return out->len;
}

static void front_connector(nl_stream_t *s, nl_buf_t *buf)
{
    int rc;
    stream_tunnel_t *t;
    nl_address_t addr;

    uint16_t port;
    uint8_t name_len;
    char name[256];

    t = s->data;

    port = ntohs(*(uint16_t *)buf->buf);
    name_len = *(uint8_t *)(buf->buf + sizeof(port));

    memcpy(name, buf->buf + sizeof(port) + sizeof(name_len), name_len);
    name[name_len] = 0;

    nl_address_setname(&addr, name);
    nl_address_setport(&addr, port);

    log_debug("#%d ready to connect to %s:%u", s->sock.fd, name, (unsigned)port);

    rc = nl_stream_connect(&t->back, &addr);
    if (rc < 0) {
        // TODO:
    }

    nl_stream_decoder_pop_back(s);
    s->cbs.on_received = on_received;
}

void on_accepted(nl_stream_t *s)
{
    int                 rc;
    acceptor_data_t     *acc_data;
    stream_tunnel_t     *t;
    nl_stream_t         *ss, *cs;
    nl_address_t        addr;

    log_trace("#%d on_accepted", s->sock.fd);

    acc_data = s->data;

    /* front side */
    t = stream_tunnel_create();
    if (t == NULL) {
        //TODO:
    }
    ss = &t->front;

    rc = nl_stream_accept(s, ss);
    if (rc == -1) {
        //TODO:
    }
    ss->data = t;

    rc = nl_socket_getpeername(&ss->sock, &addr);
    if (tun_is_connect_side() && rc == 0) {
        const char *str = nl_address_tostring(&addr);
        if (str != NULL) {
            log_info("#%d from %s", ss->sock.fd, str);
        }
    }

    ss->cbs.on_received = on_received;
    ss->cbs.on_sent = on_sent;
    ss->cbs.on_closed = on_closed;
    if (tun_is_accept_side()) {
        if (tun_need_obscure()) {
            nl_stream_encoder_push_back(ss, acc_encode, &t->o);
            nl_stream_decoder_push_back(ss, acc_splitter, &t->o);
        }
        nl_stream_decoder_push_back(ss, front_splitter, NULL);
        ss->cbs.on_received = front_connector;
    }

    nl_event_add(&ss->sock.rev);

    /* back side */
    cs = &t->back;

    rc = nl_stream(cs);
    if (rc == -1) {
        //TODO:
    }
    cs->data = t;

    cs->cbs.on_received = on_received;
    cs->cbs.on_sent = on_sent;
    cs->cbs.on_closed = on_closed;
    if (tun_is_connect_side()) {
        if (tun_need_obscure()) {
            nl_stream_encoder_push_back(cs, con_encode, &t->o);
            nl_stream_decoder_push_back(cs, con_splitter, &t->o);
        }
    }

    /* done */

    if (tun_is_connect_side()) {
        uint16_t port;
        uint8_t name_len;
        const char *name;
        nl_buf_t buf;
        char tosend[256];
        nl_address_t *to;

        gettimeofday(&t->begin, NULL);
        cs->cbs.on_connected = on_connected;
        rc = nl_stream_connect(cs, acc_data->via);
        if (rc < 0) {
            // TODO:
        }

        to = acc_data->to;
        name = nl_address_getname(to);
        port = htons(nl_address_getport(to));

        name_len = strlen(name);

        memcpy(tosend, &port, sizeof(port));
        memcpy(tosend + sizeof(port), &name_len, sizeof(name_len));
        memcpy(tosend + sizeof(port) + sizeof(name_len), name, name_len);

        buf.buf = tosend;
        buf.len = sizeof(port) + sizeof(name_len) + name_len;
        if (buf.len > 255) {
            // TODO:
        }
        rc = nl_stream_send(cs, &buf);
        if (rc < 0) {
            // TODO:
        }
    }
}

static void on_received(nl_stream_t *s, nl_buf_t *buf)
{
    int rc;
    stream_tunnel_t *t;

    log_trace("#%d on_received", s->sock.fd);

    t = s->data;
    if (s == &t->front ? t->back_closed : t->front_closed) {
        return;
    }

    rc = nl_stream_send(s == &t->front ? &t->back : &t->front, buf);
    if (rc < 0) {
        // TODO:
    }

    rc = nl_stream_pending_bytes(s);
    if (rc > SEND_UPPER_BOUND && (s == &t->front ? !t->front_paused : !t->back_paused)) {
        if (s == &t->front) {
            t->front_paused = 1;
        }
        else {
            t->back_paused = 1;
        }
        nl_stream_pause_receiving(s);
        log_debug("#%d paused @ %d", s->sock.fd, rc);
    }
}

static void on_connected(nl_stream_t *s)
{
    stream_tunnel_t *t;
    struct timeval end, cost;

    t = s->data;

    gettimeofday(&end, NULL);

    timersub(&end, &t->begin, &cost);

    log_info("#%d connect cost: %d.%ds", s->sock.fd, (int)cost.tv_sec, (int)(cost.tv_usec / 100000));
    nl_event_add(&s->sock.rev);
}

static void on_sent(nl_stream_t *s, nl_buf_t *buf)
{
    int rc;
    stream_tunnel_t *t;

    rc = nl_stream_pending_bytes(s);
    if (rc < SEND_LOWER_BOUND) {
        t = s->data;
        if (s == &t->front ? !t->back_closed && t->back_paused: !t->front_closed && t->front_paused) {
            if (s == &t->front) {
                t->back_paused = 0;
            }
            else {
                t->front_paused = 0;
            }
            nl_stream_resume_receiving(s == &t->front ? &t->back : &t->front);
            log_debug("#%d resume @ %d", s == &t->front ? t->back.sock.fd : t->front.sock.fd, rc);
        }
    }
}

static void on_closed(nl_stream_t *s)
{
    stream_tunnel_t *t;

    t = s->data;

    if (s == &t->front) {
        t->front_closed = 1;
    }
    else {
        t->back_closed = 1;
    }

    if (!(s == &t->front ? t->back_closed : t->front_closed)) {
        nl_stream_close(s == &t->front ? &t->back : &t->front);
    }
    else {
        /* both front & back has been closed */
        obscure_destroy(&t->o);
        free(t);
    }
}


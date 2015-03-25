#include "config.h"
#include "tunnel.h"
#include "obscure.h"
#include "log.h"

#include <string.h>

#define SEND_UPPER_BOUND (8*1024)
#define SEND_LOWER_BOUND (4*1024)

static void on_received(nl_stream_t *s, nl_buf_t *buf);
static void on_connected(nl_stream_t *s);
static void on_sent(nl_stream_t *s, size_t len);
static void on_closed(nl_stream_t *s);

static stream_tunnel_t *stream_tunnel_create()
{
    stream_tunnel_t *t;

    t = calloc(1, sizeof(stream_tunnel_t));
    if (t == NULL) {
        log_error("#- calloc failed");
        return NULL;
    }

    obscure_create(&t->o);

    return t;
}

static void stream_tunnel_destroy(stream_tunnel_t *t)
{
    obscure_destroy(&t->o);
    free(t);
}

#define MAX_DESTINATION 256

static int destination_splitter(void *data, const nl_buf_t *in, nl_buf_t *out)
{
    uint16_t name_len;

    if (in->len < sizeof(uint16_t) + sizeof(name_len)) {
        return 0;
    }

    name_len = *(uint16_t *)(in->buf + sizeof(uint16_t));
    if (name_len > MAX_DESTINATION) {
        log_error("name_len to big, something wrong!");
        return -1;
    }
    if (in->len < sizeof(uint16_t) + sizeof(name_len) + name_len) {
        return 0;
    }

    out->buf = in->buf;
    out->len = sizeof(uint16_t) + sizeof(name_len) + name_len;

    return out->len;
}

static void on_received_destination(nl_stream_t *s, nl_buf_t *buf)
{
    int rc;
    stream_tunnel_t *t;
    nl_address_t addr;

    uint16_t port, name_len;
    char name[MAX_DESTINATION + 1];

    t = s->data;

    port = ntohs(*(uint16_t *)buf->buf);
    name_len = *(uint16_t *)(buf->buf + sizeof(port));

    memcpy(name, buf->buf + sizeof(port) + sizeof(name_len), name_len);
    name[name_len] = 0;

    nl_address_setname(&addr, name);
    nl_address_setport(&addr, port);

    log_debug("#%d ready to connect to %s:%u", s->id, name, (unsigned)port);

    rc = nl_stream_connect(&t->back, &addr);
    if (rc < 0) {
        nl_stream_close(&t->back);
        return;
    }

    nl_stream_decoder_pop_back(s);
    s->cbs.on_received = on_received;
}

static int send_destination(nl_stream_t *s, nl_address_t *to)
{
    int rc;
    uint16_t port, name_len;
    const char *name;
    nl_buf_t buf;
    char tosend[sizeof(port) + sizeof(name_len) + MAX_DESTINATION];

    name = nl_address_getname(to);
    port = htons(nl_address_getport(to));

    /* trust arg input here */
    name_len = strlen(name);

    memcpy(tosend, &port, sizeof(port));
    memcpy(tosend + sizeof(port), &name_len, sizeof(name_len));
    memcpy(tosend + sizeof(port) + sizeof(name_len), name, name_len);

    buf.buf = tosend;
    buf.len = sizeof(port) + sizeof(name_len) + name_len;
    if (buf.len > MAX_DESTINATION) {
        log_error("#%d name(%s) too long", s->id, name);
        return -1;
    }
    rc = nl_stream_send(s, &buf);
    if (rc < 0) {
        return -1;
    }

    return 0;
}

void on_accepted(nl_stream_t *s)
{
    int                 rc;
    acceptor_data_t     *acc_data;
    stream_tunnel_t     *t;
    nl_stream_t         *fs, *bs;
    nl_address_t        addr;

    log_trace("#%d on_accepted", s->id);

    acc_data = s->data;

    /* front side */
    t = stream_tunnel_create();
    if (t == NULL) {
        return;
    }
    fs = &t->front;

    rc = nl_stream_accept(s, fs);
    if (rc == -1) {
        stream_tunnel_destroy(t);
        return;
    }
    fs->data = t;

    rc = nl_stream_getpeername(fs, &addr);
    if (tun_is_connect_side() && rc == 0) {
        const char *str = nl_address_tostring(&addr);
        if (str != NULL) {
            log_info("#%d from %s", fs->id, str);
        }
    }

    fs->cbs.on_received = on_received;
    fs->cbs.on_sent = on_sent;
    fs->cbs.on_closed = on_closed;
    if (tun_is_accept_side()) {
        if (tun_need_obscure()) {
            rc = nl_stream_encoder_push_back(fs, acc_encode, &t->o);
            if (rc == -1) {
                t->back_closed = 1;
                nl_stream_close(fs);
                return;
            }
            rc = nl_stream_decoder_push_back(fs, acc_splitter, &t->o);
            if (rc == -1) {
                t->back_closed = 1;
                nl_stream_close(fs);
                return;
            }
        }
        rc = nl_stream_decoder_push_back(fs, destination_splitter, NULL);
        if (rc == -1) {
            t->back_closed = 1;
            nl_stream_close(fs);
            return;
        }
        fs->cbs.on_received = on_received_destination;
    }

    nl_stream_resume_receiving(fs);

    /* back side */
    bs = &t->back;

    rc = nl_stream(bs);
    if (rc == -1) {
        t->back_closed = 1;
        nl_stream_close(fs);
        return;
    }
    bs->data = t;

    bs->cbs.on_received = on_received;
    bs->cbs.on_sent = on_sent;
    bs->cbs.on_closed = on_closed;
    if (tun_is_connect_side()) {
        if (tun_need_obscure()) {
            rc = nl_stream_encoder_push_back(bs, con_encode, &t->o);
            if (rc == -1) {
                nl_stream_close(fs);
                nl_stream_close(bs);
                return;
            }
            rc = nl_stream_decoder_push_back(bs, con_splitter, &t->o);
            if (rc == -1) {
                nl_stream_close(fs);
                nl_stream_close(bs);
                return;
            }
        }
    }

    /* done */
    if (tun_is_connect_side()) {
        gettimeofday(&t->begin, NULL);
        bs->cbs.on_connected = on_connected;
        rc = nl_stream_connect(bs, acc_data->via);
        if (rc < 0) {
            nl_stream_close(bs);
            return;
        }

        rc = send_destination(bs, acc_data->to);
        if (rc == -1) {
            nl_stream_close(bs);
            return;
        }
    }
}

static void on_received(nl_stream_t *s, nl_buf_t *buf)
{
    int rc;
    stream_tunnel_t *t;

    log_trace("#%d on_received", s->id);

    t = s->data;
    if (s == &t->front ? t->back_closed : t->front_closed) {
        return;
    }

    rc = nl_stream_send(s == &t->front ? &t->back : &t->front, buf);
    if (rc < 0) {
        nl_stream_close(s == &t->front ? &t->back : &t->front);
        return;
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
        log_debug("#%d paused @ %d", s->id, rc);
    }
}

static void on_connected(nl_stream_t *s)
{
    stream_tunnel_t *t;
    struct timeval end, cost;

    log_trace("#%d on_connected", s->id);

    t = s->data;

    gettimeofday(&end, NULL);
    timersub(&end, &t->begin, &cost);

    log_info("#%d connect cost: %d.%ds", s->id, (int)cost.tv_sec, (int)(cost.tv_usec / 100000));
    nl_stream_resume_receiving(s);
}

static void on_sent(nl_stream_t *s, size_t len)
{
    int rc;
    stream_tunnel_t *t;

    log_trace("#%d on_sent", s->id);

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
            log_debug("#%d resume @ %d", s == &t->front ? t->back.id : t->front.id, rc);
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
        stream_tunnel_destroy(t);
    }
}


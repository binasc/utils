#include "config.h"
#include "tunnel.h"
#include "obscure.h"
#include "log.h"

#include <string.h>

static void on_received(nl_stream_t *c, nl_buf_t *buf);
static void on_sent(nl_stream_t *c, nl_buf_t *buf);
static void on_closed(nl_stream_t *c);

static stream_data_t *socket_data_create()
{
    stream_data_t *data;

    data = calloc(1, sizeof(stream_data_t));
    if (data == NULL) {
        return NULL;
    }

    data->oe = obscure_new();
    if (data->oe == NULL) {
        free(data);
        return NULL;
    }

    data->od = obscure_new();
    if (data->od == NULL) {
        free(data);
        return NULL;
    }

    return data;
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
    stream_data_t *data;
    nl_address_t addr;

    uint16_t port;
    uint8_t name_len;
    char name[256];

    data = s->data;

    port = ntohs(*(uint16_t *)buf->buf);
    name_len = *(uint8_t *)(buf->buf + sizeof(port));

    memcpy(name, buf->buf + sizeof(port) + sizeof(name_len), name_len);
    name[name_len] = 0;

    nl_address_setname(&addr, name);
    nl_address_setport(&addr, port);

    log_debug("#%d ready to connect to %s:%u", s->sock.fd, name, (unsigned)port);

    rc = nl_stream_connect(&data->peer->s, &addr);
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
    stream_data_t       *svr_data, *cli_data;
    nl_stream_t         *ss, *cs;

    log_trace("#%d on_accepted", s->sock.fd);

    acc_data = s->data;

    /* front side */
    svr_data =  socket_data_create();
    if (svr_data == NULL) {
        //TODO:
    }
    svr_data->front = 1;
    ss = &svr_data->s;

    rc = nl_stream_accept(s, ss);
    if (rc == -1) {
        //TODO:
    }
    ss->data = svr_data;

    ss->cbs.on_received = on_received;
    ss->cbs.on_sent = on_sent;
    ss->cbs.on_closed = on_closed;
    if (tun_is_accept_side()) {
        if (tun_need_obscure()) {
            nl_stream_encoder_push_back(ss, acc_encode, svr_data->oe);
            nl_stream_decoder_push_back(ss, acc_splitter, svr_data->od);
        }
        nl_stream_decoder_push_back(ss, front_splitter, NULL);
        svr_data->s.cbs.on_received = front_connector;
    }

    nl_event_add(&svr_data->s.sock.rev);

    /* back side */
    cli_data = socket_data_create();
    if (cli_data == NULL) {
        //TODO:
    }
    cli_data->front = 0;
    cs = &cli_data->s;

    rc = nl_stream(cs);
    if (rc == -1) {
        //TODO:
    }
    cs->data = cli_data;

    cs->cbs.on_received = on_received;
    cs->cbs.on_sent = on_sent;
    cs->cbs.on_closed = on_closed;
    if (tun_is_connect_side()) {
        if (tun_need_obscure()) {
            nl_stream_encoder_push_back(cs, con_encode, cli_data->oe);
            nl_stream_decoder_push_back(cs, con_splitter, cli_data->od);
        }
    }

    svr_data->peer = cli_data;
    cli_data->peer = svr_data;

    /* done */

    if (tun_is_connect_side()) {
        uint16_t port;
        uint8_t name_len;
        const char *name;
        nl_buf_t buf;
        char tosend[256];
        nl_address_t *to;

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
        cli_data->nsend += rc;
    }
}

static void on_received(nl_stream_t *c, nl_buf_t *buf)
{
    int rc;
    stream_data_t *data;

    log_trace("#%d on_received", c->sock.fd);

    data = c->data;
    if (data->peer == NULL) {
        return;
    }

    rc = nl_stream_send(&data->peer->s, buf);
    if (rc < 0) {
        // TODO:
    }
    data->peer->nsend += rc;

    if (!data->paused && data->peer->nsend > 0) {
        data->paused = 1;
        nl_stream_pause_receiving(c);
        log_debug("pause @ size: %zu", data->peer->nsend);
    }
}

static void on_sent(nl_stream_t *c, nl_buf_t *buf)
{
    stream_data_t *data;

    data = c->data;
    data->nsend -= buf->len;
    if (data->nsend == 0) {
        if (data->peer && data->peer->paused) {
            data->peer->paused = 0;
            nl_stream_resume_receiving(&data->peer->s);
            log_debug("resume @ size: %zu", data->nsend);
        }
    }
}

static void on_closed(nl_stream_t *c)
{
    stream_data_t *data, *peer;

    data = c->data;

    if (data->peer != NULL) {
        peer = data->peer;
        peer->peer = NULL;
        data->peer = NULL;
        nl_stream_close(&peer->s);
    }
    obscure_free(data->oe);
    obscure_free(data->od);
    free(data);
}


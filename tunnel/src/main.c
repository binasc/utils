#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <assert.h>

#include "utils.h"
#include "network.h"
#include "obscure.h"
#include "tunnel.h"
#include "log.h"

static int s_acc_obs = 0;
static int s_con_obs = 0;

static int s_daemon;
static int s_partner;

static char s_lhost[256];
static char s_rhost[256];
static uint16_t s_rport;
static uint16_t s_lport;

static int s_udp;

int options(int argc, char ** argv)
{
    int opt;
    int version = 0;

    while ((opt = getopt(argc, argv, "dpl:r:vACu")) != EOF)
    {
        switch (opt)
        {
            case 'd':
                s_daemon = 1;
                break;
            case 'p':
                s_partner = 1;
                break;
            case 'l':
                strcpy(s_lhost, optarg);
                break;
            case 'r':
                strcpy(s_rhost, optarg);
                break;
            case 'v' :
                version = 1;
                break;
            case 'A':
                s_acc_obs = 1;
                break;
            case 'C':
                s_con_obs = 1;
                break;
            case 'u':
                s_udp = 1;
                break;
            default :
                log_error("invalid option -- '%c'", opt);
                return -1;
        }
    }

    if (version)
    {
        printf("tunnel build in %s %s\n", __DATE__, __TIME__);
        return -1;
    }

    return 0;
}

socket_data_t *socket_data_create()
{
    socket_data_t *data;

    data = calloc(1, sizeof(socket_data_t));
    if (data == NULL) {
        return NULL;
    }

    data->o = obscure_new();
    if (data->o == NULL) {
        free(data);
        return NULL;
    }

    return data;
}

void on_accepted(nl_connection_t *c, nl_connection_t *nc);
void on_received(nl_connection_t *c, nl_buf_t *buf);
void on_sent(nl_connection_t *c, nl_buf_t *buf);
void on_closed(nl_connection_t *c);

void on_accepted(nl_connection_t *c, nl_connection_t *nc)
{
    int                 rc;
    socket_data_t       *svr_data, *cli_data;
    nl_connection_t     *cc;

    /* accept side */
    svr_data =  socket_data_create();
    if (svr_data == NULL) {
    }
    svr_data->c = nc;
    svr_data->side = ACCEPT_SIDE;

    nc->cbs.on_received = on_received;
    nc->cbs.on_sent = on_sent;
    nc->cbs.on_closed = on_closed;
    if (s_acc_obs) {
        nc->cbs.splitter = acc_splitter;
    }
    nc->data = svr_data;

    nl_event_add(&nc->sock.rev);

    /* connect side */
    cc = nl_connection();
    if (cc == NULL) {
    }

    cli_data = socket_data_create();
    if (cli_data == NULL) {
    }
    cli_data->c = cc;
    cli_data->side = CONNECT_SIDE;

    cc->cbs.on_received = on_received;
    cc->cbs.on_sent = on_sent;
    cc->cbs.on_closed = on_closed;
    if (s_con_obs) {
        cc->cbs.splitter = con_splitter;
    }
    cc->data = cli_data;

    svr_data->peer = cli_data;
    cli_data->peer = svr_data;

    rc = nl_connection_connect(cc, (struct sockaddr_in *)c->data);
    if (rc < 0) {
    }
}

void on_received(nl_connection_t *c, nl_buf_t *buf)
{
    socket_data_t *data;

    data = c->data;
    if (data->peer == NULL) {
        return;
    }

    if (s_acc_obs && data->side == CONNECT_SIDE) {
        buf->buf = acc_encode(data->o, buf->buf, &buf->len);
    }
    else if (s_con_obs && data->side == ACCEPT_SIDE) {
        buf->buf = con_encode(data->o, buf->buf, &buf->len);
    }
    else if (s_acc_obs && data->side == ACCEPT_SIDE) {
        buf->buf = acc_decode(data->o, buf->buf, &buf->len);
    }
    else if (s_con_obs && data->side == CONNECT_SIDE) {
        buf->buf = con_decode(data->o, buf->buf, &buf->len);
    }

#define SEND_BUFF_SIZE 16384
    nl_connection_send(data->peer->c, buf);
    data->peer->nsend += buf->len;

    if (!data->paused && data->peer->nsend > SEND_BUFF_SIZE) {
        data->paused = 1;
        nl_connection_pause_receiving(c);
        log_debug("pause @ size: %zu", data->peer->nsend);
    }
}

void on_sent(nl_connection_t *c, nl_buf_t *buf)
{
    socket_data_t *data;

    data = c->data;
    data->nsend -= buf->len;
    if (data->nsend <= SEND_BUFF_SIZE) {
        if (data->peer && data->peer->paused) {
            data->peer->paused = 0;
            nl_connection_resume_receiving(data->peer->c);
            log_debug("resume @ size: %zu", data->nsend);
        }
    }
}

void on_closed(nl_connection_t *c)
{
    socket_data_t *data, *peer;

    data = c->data;

    if (data->peer != NULL) {
        peer = data->peer;
        peer->peer = NULL;
        data->peer = NULL;
        nl_connection_close(peer->c);
    }
    obscure_free(data->o);
    free(data);
}

static datagram_data_t *udp_addr2data;

static datagram_data_t *find_datagram(struct sockaddr_in *addr)
{
    datagram_data_t *d;

    for (d = udp_addr2data; d != NULL; d = d->next) {
        if (d->peer.sin_addr.s_addr == addr->sin_addr.s_addr &&
            d->peer.sin_port == addr->sin_port) {
            return d;
        }
    }

    return NULL;
}

static void push_datagram(datagram_data_t *d)
{
    d->next = udp_addr2data;
    udp_addr2data = d;
}

static void erase_datagram(struct sockaddr_in *addr)
{
    datagram_data_t **last, *d;

    last = &udp_addr2data;
    for (d = udp_addr2data; d != NULL; d = d->next) {
        if (d->peer.sin_addr.s_addr == addr->sin_addr.s_addr &&
            d->peer.sin_port == addr->sin_port) {
            *last = d->next;
            return;
        }

        last = &d->next;
    }

    log_warning("#%d datagram not found", d->d.sock.fd);
}

static void on_udp_closed(nl_datagram_t *d)
{
    datagram_data_t *data;

    data = d->data;

    erase_datagram(&data->peer);

    log_debug("#%d datagram destroyed", d->sock.fd);

    free(data);
}

// 10 min
#define UDP_TIMEOUT (10 * 60 * 1000)

static void on_udp_received(nl_datagram_t *d, nl_packet_t *p)
{
    datagram_data_t *data;

    data = d->data;

    memcpy(&p->addr, &data->peer, sizeof(data->peer));

    if (s_con_obs) {
        p->buf.buf = udp_con_decode(&data->con_o, p->buf.buf, &p->buf.len);
    }
    else if (s_acc_obs) {
        p->buf.buf = udp_acc_encode(&data->con_o, p->buf.buf, &p->buf.len);
    }

    if (p->buf.buf != NULL) {
        nl_datagram_send(data->acceptor, p);
    }

    nl_event_del_timer(&data->timeout);
    nl_event_add_timer(&data->timeout, UDP_TIMEOUT);
}

void on_udp_sent(nl_datagram_t *d, nl_packet_t *p)
{
}

void on_udp_timeout(nl_event_t *ev)
{
    datagram_data_t *d;

    d = ev->data;

    log_debug("#%d timeout", d->d.sock.fd);

    nl_datagram_close(&d->d);
}

void on_udp_accepted(nl_datagram_t *d, nl_packet_t *p)
{
    int rc;
    datagram_data_t *association;

    association = find_datagram(&p->addr);
    if (association == NULL) {
        association = malloc(sizeof(datagram_data_t));
        if (association == NULL) {
            log_error("malloc() failed");
            return;
        }
        udp_obscure(&association->acc_o);
        udp_obscure(&association->con_o);

        memcpy(&association->peer, &p->addr, sizeof(p->addr));
        rc = nl_datagram(&association->d);
        if (rc < 0) {
            free(association);
        }

        association->d.on_received = on_udp_received;
        association->d.on_sent = on_udp_sent;
        association->d.on_closed = on_udp_closed;
        association->d.data = association;
        association->acceptor = d;

        association->timeout.data = association;
        association->timeout.handler = on_udp_timeout;

        nl_event_add(&association->d.sock.rev);
        //nl_event_add(&association->d.sock.wev);
        push_datagram(association);
    }

    memcpy(&p->addr, d->data, sizeof(p->addr));

    if (s_con_obs) {
        p->buf.buf = udp_con_encode(&association->acc_o, p->buf.buf, &p->buf.len);
    }
    else if (s_acc_obs) {
        p->buf.buf = udp_acc_decode(&association->acc_o, p->buf.buf, &p->buf.len);
    }

    // TODO: discard or send anyway?
    if (p->buf.buf != NULL) {
        nl_datagram_send(&association->d, p);
    }

    nl_event_del_timer(&association->timeout);
    nl_event_add_timer(&association->timeout, UDP_TIMEOUT);
}

int main(int argc, char *argv[])
{
    int rc;
    char *colon;
    struct sockaddr_in local_addr, remote_addr;

    srand(time(NULL));

    rc = options(argc, argv);
    if (rc == -1) {
        return -1;
    }

    if (s_daemon) {
        utils_daemon(".");
    }

    if (s_partner) {
        utils_partner("tunnel.pid", argv);
    }

    if ((colon = strchr(s_lhost, ':')) == NULL) {
        fprintf(stderr, "invalid argument: %s\n", s_lhost);
        return -1;
    }
    *colon = 0;
    if ((s_lport = atoi(colon + 1)) == 0) {
        fprintf(stderr, "invalid argument: %s\n", s_lhost);
        return -1;
    }
    if ((colon = strchr(s_rhost, ':')) == NULL) {
        fprintf(stderr, "invalid argument: %s\n", s_rhost);
        return -1;
    }
    *colon = 0;
    if ((s_rport = atoi(colon + 1)) == 0) {
        fprintf(stderr, "invalid argument: %s\n", s_rhost);
        return -1;
    }

    rc = nl_event_init();
    if (rc < 0) {
        return -1;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(s_lport);
    rc = nl_queryname(s_lhost, &local_addr.sin_addr);
    if (rc < 0) {
        return -1;
    }

    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(s_rport);
    rc = nl_queryname(s_rhost, &remote_addr.sin_addr);
    if (rc < 0) {
        return -1;
    }

    if (s_udp) {
        nl_datagram_t d;

        rc = nl_datagram(&d);
        if (rc < 0) {
            return -1;
        }

        rc = nl_datagram_bind(&d, &local_addr);
        if (rc < 0) {
            return -1;
        }

        d.on_received = on_udp_accepted;
        d.on_sent = on_udp_sent;
        d.data = &remote_addr;

        nl_event_add(&d.sock.rev);
        nl_event_add(&d.sock.wev);
    }
    else {
        nl_connection_t *c;

        c = nl_connection();
        if (c == NULL) {
            return -1;
        }
        c->cbs.on_accepted = on_accepted;
        c->data = &remote_addr;

        rc = nl_connection_listen(c, &local_addr, 1);
        if (rc < 0) {
            return -1;
        }
    }

    nl_process_loop();

    return 0;
}


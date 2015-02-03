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

static char s_lhosts[256];
static char s_rhosts[256];

static int s_udp;

static struct {
    struct sockaddr_in  addr;
    int                 id;
} s_laddrs[10], s_raddrs[10];
static int s_nladdrs, s_nraddrs;

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
                strcpy(s_lhosts, optarg);
                break;
            case 'r':
                strcpy(s_rhosts, optarg);
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
    acceptor_data_t     *acc_data;
    socket_data_t       *svr_data, *cli_data;
    nl_connection_t     *cc;

    acc_data = c->data;

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

    if (!s_acc_obs) {
        rc = nl_connection_connect(cc, &acc_data->remote);
        if (rc < 0) {
        }
    }
    if (s_con_obs) {
        uint16_t id;
        nl_buf_t buf;

        id = htons(acc_data->id);
        buf.len = sizeof(id);
        log_debug("id: %d", acc_data->id);
        buf.buf = con_encode(svr_data->o, &id, &buf.len);
        nl_connection_send(cc, &buf);
        cli_data->nsend += buf.len;
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

    if (s_acc_obs && data->side == ACCEPT_SIDE && data->o->id == -1) {
        int rc;

        data->o->id = ntohs(*(uint16_t *)buf->buf);
        log_debug("id: %d", data->o->id);
        buf->buf += sizeof(uint16_t);
        buf->len -= sizeof(uint16_t);

        rc = nl_connection_connect(data->peer->c, &s_raddrs[data->o->id].addr);
        if (rc < 0) {
        }
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
    if (data->timeout.timer_set) {
        nl_event_del_timer(&data->timeout);
    }

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
        memset(association, 0, sizeof(datagram_data_t));
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

int parse_addrs(char *arg, struct sockaddr_in *addr, int *id_)
{
    int rc, len;
    char *end, *port, *id;

    end = strchr(arg, '/');
    if (end == NULL) {
        len = strlen(arg);
        if (len == 0) {
            return 0;
        }
    }
    else {
        len = end - arg;
        *end = 0;
    }

    port = strchr(arg, ':');
    if (port == NULL || port - arg > len) {
        fprintf(stderr, "invalid argument: %s\n", arg);
        return -1;
    }
    *port = 0;

    if (id_) {
        id = strchr(port + 1, ':');
        if (id == NULL || id - arg > len) {
            fprintf(stderr, "invalid argument: %s\n", arg);
            return -1;
        }
        *id = 0;
        *id_ = atoi(id + 1);
    }

    addr->sin_port = htons(atoi(port + 1));
    rc = nl_queryname(arg, &addr->sin_addr);
    if (rc < 0) {
        return -1;
    }

    return len + 1;
}

int main(int argc, char *argv[])
{
    int rc, len;
    char *paddr;

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

    srand(time(NULL));

    paddr = s_lhosts;
    for ( ; ; ) {
        if (s_con_obs) {
            len = parse_addrs(paddr, &s_laddrs[s_nladdrs].addr, &s_laddrs[s_nladdrs].id);
            log_debug("addr: %s, id: %d", inet_ntoa(s_laddrs[s_nladdrs].addr.sin_addr), s_laddrs[s_nladdrs].id);
        }
        else {
            len = parse_addrs(paddr, &s_laddrs[s_nladdrs].addr, NULL);
        }
        if (len < 0) {
            return -1;
        }
        else if (len == 0) {
            break;
        }

        s_nladdrs++;
        paddr += len;
    }

    paddr = s_rhosts;
    for ( ; ; ) {
        len = parse_addrs(paddr, &s_raddrs[s_nraddrs].addr, NULL);
        if (len < 0) {
            return -1;
        }
        else if (len == 0) {
            break;
        }

        s_nraddrs++;
        paddr += len;
    }

    nl_event_init();

    if (s_udp) {
        nl_datagram_t d;

        rc = nl_datagram(&d);
        if (rc < 0) {
            return -1;
        }

        // TODO: udp support multiple tunnels
        rc = nl_datagram_bind(&d, &s_laddrs[0].addr);
        if (rc < 0) {
            return -1;
        }

        d.on_received = on_udp_accepted;
        d.on_sent = on_udp_sent;
        d.data = &s_raddrs[0].addr;

        nl_event_add(&d.sock.rev);
        nl_event_add(&d.sock.wev);
    }
    else {
        int i;
        for (i = 0; i < s_nladdrs; i++) {
            nl_connection_t *c;
            acceptor_data_t *acc_data;

            c = nl_connection();
            if (c == NULL) {
                return -1;
            }

            acc_data = malloc(sizeof(acceptor_data_t));
            if (acc_data == NULL) {
                return -1;
            }

            // TODO: support multiple remote tunnels
            acc_data->id = s_laddrs[i].id;
            memcpy(&acc_data->remote, &s_raddrs[0].addr, sizeof(struct sockaddr_in));
            c->cbs.on_accepted = on_accepted;
            c->data = acc_data;

            rc = nl_connection_listen(c, &s_laddrs[i].addr, 10);
            if (rc < 0) {
                return -1;
            }
        }
    }

    nl_process_loop();

    return 0;
}


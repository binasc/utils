#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <assert.h>

#include "utils.h"
#include "network.h"
#include "list.h"

#define log_error(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#define log_debug(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
//#define log_debug(fmt, ...)
#define log_trace(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
//#define log_trace(fmt, ...)

static int s_acc_obs = 0;
static int s_con_obs = 0;

static int s_daemon;
static int s_partner;

static char s_host[256];
static uint16_t s_rport;
static uint16_t s_lport;

int options(int argc, char ** argv)
{
    int iOpt = 0;
    int iQurVer = 0;

    while ((iOpt = getopt(argc, argv, "dPh:p:l:vAC")) != EOF)
    {
        switch (iOpt)
        {
            case 'd':
                s_daemon = 1;
                break;
            case 'P':
                s_partner = 1;
                break;
            case 'h':
                strcpy(s_host, optarg);
                break;
            case 'p':
                s_rport = atoi(optarg);
                break;
            case 'l':
                s_lport = atoi(optarg);
                break;
            case 'v' :
                iQurVer = 1;
                break;
            case 'A':
                s_acc_obs = 1;
                break;
            case 'C':
                s_con_obs = 1;
                break;
            default :
                log_error("invalid option -- '%c'", iOpt);
                return -1;
        }
    }

    if (iQurVer)
    {
        printf("nlog server build in %s %s\n", __DATE__, __TIME__);
        return -1;
    }

    return 0;
}

typedef struct accept_data_s
{
    struct sockaddr_in remote_addr;
} accept_data_t;

struct tunnel_data_s;

typedef struct socket_data_s
{
    struct socket_data_s    *peer;
    select_socket_t         *sock;
    struct list_t           *send_list;
    struct tunnel_data_s    *tun_data;
} socket_data_t;

typedef struct buf_s
{
    char *buf;
    size_t len;
} buf_t;

typedef struct tunnel_data_s
{
    int             svr_closing;
    int             cli_closing;
    socket_data_t   svr_data;
    socket_data_t   cli_data;
    select_event_t  closing_ev;
} tunnel_data_t;

tunnel_data_t *tunnel_data_create()
{
    tunnel_data_t *data;

    data = calloc(1, sizeof(tunnel_data_t));
    if (data == NULL) {
        return NULL;
    }

    data->svr_data.send_list = list_create(sizeof(buf_t), NULL, NULL);
    if (data->svr_data.send_list == NULL) {
        free(data);
        return NULL;
    }

    data->cli_data.send_list = list_create(sizeof(buf_t), NULL, NULL);
    if (data->cli_data.send_list == NULL) {
        list_destroy(data->svr_data.send_list);
        free(data);
        return NULL;
    }

    data->svr_data.peer = &data->cli_data;
    data->cli_data.peer = &data->svr_data;
    data->svr_data.tun_data = data;
    data->cli_data.tun_data = data;

    return data;
}

void tunnel_data_destroy(tunnel_data_t *data)
{
    list_destroy(data->svr_data.send_list);
    list_destroy(data->cli_data.send_list);
    nl_select_close(data->svr_data.sock);
    nl_select_close(data->cli_data.sock);
    free(data);
}

void tunnel_data_timeout(select_event_t *ev)
{
    tunnel_data_destroy(ev->data);
}

void tunnel_data_close(tunnel_data_t *data, socket_data_t *peer)
{
    if (&data->cli_data == peer) {
        if (data->svr_closing) {
            return;
        }
        data->svr_closing = 1;
        puts("svr closing");
    }
    else {
        if (data->cli_closing) {
            return;
        }
        data->cli_closing = 1;
        puts("cli closing");
    }

    if (data->svr_closing && data->cli_closing) {
        assert(data->closing_ev.timer_set);

        puts("del event");
        nl_select_del_event(&data->closing_ev);
        tunnel_data_destroy(data);
        return;
    }

    assert(!data->closing_ev.timer_set);
    if (list_empty(peer->send_list)) {
        tunnel_data_destroy(data);
    }
    else {
        data->closing_ev.handler = tunnel_data_timeout;
        data->closing_ev.data = data;
        puts("add event");
        printf("list len: %d\n", list_length(peer->send_list));
        nl_select_add_event(peer->sock->ctx, &data->closing_ev, 5000);
    }
}

int server_accept(select_t *ctx, select_socket_t *sock);
int server_receive(select_t *ctx, select_socket_t *sock);
int server_send(select_t *ctx, select_socket_t *sock);
int client_connected(select_t *ctx, select_socket_t *sock);
int client_receive(select_t *ctx, select_socket_t *sock);
int client_send(select_t *ctx, select_socket_t *sock);

int server_accept(select_t *ctx, select_socket_t *sock)
{
    int                 rc;
    select_op_t         svr_ops, cli_ops;
    select_socket_t     *svr_sock, *cli_sock;
    accept_data_t       *acc_data;
    tunnel_data_t       *tun_data;

    tun_data = tunnel_data_create();
    if (tun_data == NULL) {
        return 0;
    }

    svr_ops.receive = server_receive;
    svr_ops.send = server_send;

    svr_sock = nl_select_accept(sock, &svr_ops, &tun_data->svr_data);
    if (svr_sock == NULL) {
        tunnel_data_destroy(tun_data);
        return 1;
    }
    tun_data->svr_data.sock = svr_sock;
    nl_select_begin_recv(svr_sock);

    cli_ops.connected = client_connected;
    cli_ops.receive = client_receive;
    cli_ops.send = client_send;

    cli_sock = nl_select_socket(ctx, &cli_ops, &tun_data->cli_data);
    if (cli_sock == NULL) {
        tunnel_data_destroy(tun_data);
        return 1;
    }
    tun_data->cli_data.sock = cli_sock;

    acc_data = (accept_data_t *)sock->data;
    rc = nl_select_connect(cli_sock, &acc_data->remote_addr);
    if (rc < 0) {
        tunnel_data_destroy(tun_data);
        return 1;
    }

    return 1;
}

int receive_and_send(select_t *ctx, select_socket_t *sock)
{
    int rc, nread;
    socket_data_t *rdata, *sdata;
    buf_t buf;

    rdata = (socket_data_t *)sock->data;
    sdata = (socket_data_t *)rdata->peer;

    nread = 0;
    while (1) {
        buf.buf = (char *)malloc(4096);
        if (buf.buf == NULL) {
            break;
        }

        rc = nl_select_recv(sock, buf.buf, 4096);
        if (rc == EAGAIN) {
            free(buf.buf);
            ///log_debug("recv %d bytes", nread);
            return 1;
        }
        else if (rc <= 0) {
            free(buf.buf);
            tunnel_data_close(rdata->tun_data, sdata);
            break;
        }

        if ((s_acc_obs && &rdata->tun_data->cli_data == rdata)
            || (s_con_obs && &rdata->tun_data->svr_data == rdata)) {
            // encode
        }
        else if ((s_acc_obs && &rdata->tun_data->svr_data == rdata)
                 || (s_con_obs && &rdata->tun_data->cli_data == rdata)) {
            // decode
        }

        nread += rc;
        buf.len = rc;
        list_push_back(sdata->send_list, &buf);
        buf = *(buf_t *)list_front(sdata->send_list);
        nl_select_begin_send(sdata->sock);
    }

    //log_debug("recv %d bytes", nread);
    return 0;
}

int send_and_send(select_t *ctx, select_socket_t *sock)
{
    int rc, nsent, peer_closing;
    socket_data_t *data = (socket_data_t *)sock->data;

    nsent = 0;
    while (!list_empty(data->send_list)) {
        buf_t *buf = (buf_t *)list_front(data->send_list);
        rc = nl_select_send(sock, buf->buf, buf->len);
        if (rc == EAGAIN) {
            break;
        }
        else if (rc < 0) {
            tunnel_data_close(data->tun_data, data->peer);
            return 0;
        }
        else if (rc == (int)buf->len) {
            free(buf->buf);
            list_pop_front(data->send_list);
        }
        else {
            memmove(buf->buf, buf->buf + rc, buf->len - rc);
            buf->len -= rc;
        }
        nsent += rc;
    }


    if (data->tun_data->svr_data.sock == sock) {
        peer_closing = data->tun_data->cli_closing;
    }
    else {
        peer_closing = data->tun_data->svr_closing;
    }
    rc = list_empty(data->send_list) ? 0 : 1;
    if (peer_closing && rc == 0) {
        assert(data->tun_data->closing_ev.timer_set);

        puts("del event");
        nl_select_del_event(&data->tun_data->closing_ev);
        tunnel_data_destroy(data->tun_data);
    }

    //log_debug("sent %d bytes", nsent);
    return rc;
}

int server_receive(select_t *ctx, select_socket_t *sock)
{
    return receive_and_send(ctx, sock);
}

int server_send(select_t *ctx, select_socket_t *sock)
{
    return send_and_send(ctx, sock);
}

int client_connected(select_t *ctx, select_socket_t *sock)
{
    socket_data_t *data = (socket_data_t *)sock->data;

    if (!sock->connected) {
        return 0;
    }
    log_trace("%d connected", sock->fd);

    nl_select_begin_recv(sock);
    return list_empty(data->send_list) ? 0 : 1;
}

int client_receive(select_t *ctx, select_socket_t *sock)
{
    return receive_and_send(ctx, sock);
}

int client_send(select_t *ctx, select_socket_t *sock)
{
    return send_and_send(ctx, sock);
}

int main(int argc, char *argv[])
{
    int rc;

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

    select_t ctx;
    rc = nl_select_init(&ctx);
    if (rc < 0) {
    }

    accept_data_t *data;
    data = (accept_data_t *)malloc(sizeof(accept_data_t));
    if (data == NULL) {
        return -1;
    }

    memset(&data->remote_addr, 0, sizeof(struct sockaddr_in));
    data->remote_addr.sin_family = AF_INET;
    data->remote_addr.sin_port = htons(s_rport);
    rc = nl_queryname(s_host, &data->remote_addr.sin_addr);
    if (rc == -1) {
        return -1;
    }

    select_socket_t *sock;
    select_op_t ops;
    ops.accept = server_accept;
    sock = nl_select_socket(&ctx, &ops, data);
    if (sock == NULL) {
    }

    rc = nl_select_listen(sock, s_lport, 1);
    if (rc < 0) {
        return -1;
    }

    nl_select_loop(&ctx);

    return 0;
}


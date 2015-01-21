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

// TODO: congestion control

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
    accept_data_t       *acc_data;
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

    acc_data = c->data;
    rc = nl_connection_connect(cc, &acc_data->remote_addr);
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

    if (nl_connection_tosend_size(data->peer->c) > SEND_BUFF_SIZE) {
        nl_connection_pause_receiving(c);
    }
}

void on_sent(nl_connection_t *c, nl_buf_t *buf)
{
    socket_data_t *data;

    data = c->data;
    if (nl_connection_tosend_size(c) <= SEND_BUFF_SIZE) {
        if (data->peer) {
            nl_connection_resume_receiving(data->peer->c);
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

    rc = nl_event_init();
    if (rc < 0) {
    }

    accept_data_t *data;
    data = malloc(sizeof(accept_data_t));
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

    nl_connection_t *c;

    c = nl_connection();
    if (c == NULL) {
        return -1;
    }
    c->cbs.on_accepted = on_accepted;
    c->data = data;

    struct sockaddr_in local;
    memset(&local, sizeof(local), 0);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = inet_addr("0.0.0.0");
    local.sin_port = htons(s_lport);

    rc = nl_connection_listen(c, &local, 1);
    if (rc < 0) {
        return -1;
    }

    nl_process_loop();

    return 0;
}


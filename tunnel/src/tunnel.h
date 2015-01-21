#ifndef __TUNNEL_H__
#define __TUNNEL_H__

typedef struct accept_data_s
{
    struct sockaddr_in remote_addr;
} accept_data_t;

#define ACCEPT_SIDE 0
#define CONNECT_SIDE 1

typedef struct socket_data_s
{
    struct socket_data_s    *peer;
    nl_connection_t         *c;
    obscure_t               *o;
    size_t                  nsend;
    unsigned                side :1;
    unsigned                paused: 1;
} socket_data_t;

#endif


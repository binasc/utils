#ifndef __TUNNEL_H__
#define __TUNNEL_H__

#define ACCEPT_SIDE 0
#define CONNECT_SIDE 1

#include "network.h"
#include "dgram.h"

typedef struct acceptor_data_s
{
    uint16_t                id;
    nl_address_t            remote;
} acceptor_data_t;

typedef struct socket_data_s
{
    struct socket_data_s    *peer;
    nl_connection_t         *c;
    obscure_t               *o;
    size_t                  nsend;
    unsigned                side :1;
    unsigned                paused: 1;
} socket_data_t;

typedef struct datagram_data_s
{
    struct sockaddr_in      peer;
    nl_datagram_t           d;
    nl_datagram_t           *acceptor;
    struct datagram_data_s  *next;
    udp_obscure_t           acc_o;
    udp_obscure_t           con_o;
    nl_event_t              timeout;
} datagram_data_t;

#endif


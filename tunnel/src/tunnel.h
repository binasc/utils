#ifndef __TUNNEL_H__
#define __TUNNEL_H__

#include "stream.h"
#include "dgram.h"
#include "obscure.h"

typedef struct acceptor_data_s
{
    nl_address_t            *from;
    nl_address_t            *via;
    nl_address_t            *to;
} acceptor_data_t;

typedef struct stream_data_s
{
    struct stream_data_s    *peer;
    nl_stream_t             s;
    obscure_t               *oe;
    obscure_t               *od;
    size_t                  nsend;
    unsigned                front :1; /* 0 == back */
    unsigned                paused: 1;
} stream_data_t;

typedef struct datagram_data_s
{
    struct sockaddr_in      peer;
    nl_dgram_t              d;
    nl_dgram_t              *acceptor;
    struct datagram_data_s  *next;
    udp_obscure_t           acc_o;
    udp_obscure_t           con_o;
    nl_event_t              timeout;
} datagram_data_t;

#endif


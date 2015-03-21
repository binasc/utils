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

typedef struct stream_tunnel_s
{
    nl_stream_t             front;
    nl_stream_t             back;

    obscure_t               o;

    unsigned                front_closed :1;
    unsigned                back_closed :1;
    unsigned                front_paused: 1;
    unsigned                back_paused: 1;
} stream_tunnel_t;

typedef struct dgram_tunnel_s
{
    struct sockaddr_in      peer;
    nl_dgram_t              d;
    nl_dgram_t              *acceptor;
    udp_obscure_t           acc_o;
    udp_obscure_t           con_o;
    nl_event_t              timeout;

    struct dgram_tunnel_s   *next;
} dgram_tunnel_t;

#endif


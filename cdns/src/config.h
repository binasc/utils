#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "address.h"

typedef struct address_tuple_s {
    nl_address_t        from;
    nl_address_t        via;
    nl_address_t        to;
} address_tuple_t;

int tun_options(int argc, char *argv[]);

int tun_is_udp_mode();

int tun_num_address_tuple();
address_tuple_t *tun_get_address_tuple(int num);

int tun_is_accept_side();
int tun_is_connect_side();

int tun_need_obscure();

#endif


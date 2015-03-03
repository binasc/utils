#ifndef __OBSCURE_H__
#define __OBSCURE_H__

#include "stream.h"

typedef struct obscure_s
{
    nl_buf_t    remain;
    size_t      remain_size;
    size_t      to_recv_size;
    size_t      to_send_size;
    int         last_key;
    int         id;
} obscure_t;

obscure_t *obscure_new();
void obscure_free(obscure_t *o);

int acc_splitter(nl_connection_t *c, const nl_buf_t *in, nl_buf_t *out);
int con_splitter(nl_connection_t *c, const nl_buf_t *in, nl_buf_t *out);

void *acc_encode(obscure_t *o, void *buf, size_t *len);
void *con_encode(obscure_t *o, void *buf, size_t *len);
void *acc_decode(obscure_t *o, void *buf, size_t *len);
void *con_decode(obscure_t *o, void *buf, size_t *len);

typedef struct udp_obscure_s
{
    int         last_key;
} udp_obscure_t;

void udp_obscure(udp_obscure_t *o);

void *udp_acc_encode(udp_obscure_t *o, void *buf, size_t *len);
void *udp_con_encode(udp_obscure_t *o, void *buf, size_t *len);
void *udp_acc_decode(udp_obscure_t *o, void *buf, size_t *len);
void *udp_con_decode(udp_obscure_t *o, void *buf, size_t *len);

#endif


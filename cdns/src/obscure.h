#ifndef __OBSCURE_H__
#define __OBSCURE_H__

#include "stream.h"

typedef struct obscure_s
{
    nl_buf_t    remain;
    size_t      remain_size;
    size_t      to_recv_size;
    size_t      to_send_size;
    unsigned    enc_last_key;
    unsigned    dec_last_key;
} obscure_t;

void obscure_create(obscure_t *o);
void obscure_destroy(obscure_t *o);

int acc_splitter(void *, const nl_buf_t *in, nl_buf_t *out);
int con_splitter(void *, const nl_buf_t *in, nl_buf_t *out);

char *acc_encode(void *, char *buf, size_t *len);
char *con_encode(void *, char *buf, size_t *len);

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


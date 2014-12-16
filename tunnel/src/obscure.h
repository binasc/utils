#ifndef __OBSCURE_H__
#define __OBSCURE_H__

#include "network.h"

typedef struct obscure_s
{
    nl_buf_t    remain;
    size_t      remain_size;
    size_t      to_recv_size;
    size_t      to_send_size;
} obscure_t;

obscure_t *obscure_new();
void obscure_free(obscure_t *o);

int acc_splitter(nl_connection_t *c, const nl_buf_t *in, nl_buf_t *out);
int con_splitter(nl_connection_t *c, const nl_buf_t *in, nl_buf_t *out);

void *acc_encode(obscure_t *o, void *buf, size_t *len);
void *con_encode(obscure_t *o, void *buf, size_t *len);
void *acc_decode(obscure_t *o, void *buf, size_t *len);
void *con_decode(obscure_t *o, void *buf, size_t *len);

#endif


#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "obscure.h"
#include "tunnel.h"
#include "log.h"

static char *enlarge_buffer(obscure_t *o, size_t size)
{
    char *buf = NULL;

    if (size > o->remain_size) {
        buf = realloc(o->remain.buf, size);
        if (buf == NULL) {
            free(o->remain.buf);
            o->remain.buf = NULL;
            o->remain.len = 0;
            o->remain_size = 0;
        }
        else {
            o->remain.buf = buf;
            o->remain_size = size;
        }
    }

    return o->remain.buf;
}

obscure_t *obscure_new()
{
    obscure_t *o;

    o = calloc(1, sizeof(obscure_t));
    return o;
}

void obscure_free(obscure_t *o)
{
    if (o->remain_size > 0) {
        free(o->remain.buf);
    }
    memset(o, 0, sizeof(obscure_t));
}

static unsigned char s_key[4] = { 0x4a, 0x3f, 0xbc, 0x70 };

char *xor(obscure_t *o, char *buf, size_t *len)
{
    size_t i;
    for (i = 0; i < *len; i++) {
        buf[i] = ~buf[i];
        buf[i] ^= s_key[(o->last_key++) % 4];
    }
    o->last_key %= 4;

    return buf;
}

static unsigned char s_bh[] = {
    0x42, 0x4c,                 /* 'BM' */
    0x00, 0x00, 0x00, 0x00,     /* file size in byte */
    0x00, 0x00, 0x00, 0x00,     /* reserved */
    0x36, 0x00, 0x00, 0x00,     /* content offset from start of file */
    0x28, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,     /* height */
    0x01, 0x00,
    0x18, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,     /* content size in byte */
    0x12, 0x0b, 0x00, 0x00,
    0x12, 0x0b, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

#pragma pack(1)
typedef struct bmp_header_s {
    char        magic[2];
    uint32_t    size;
    uint32_t    reserved0;
    uint32_t    offset;
    uint32_t    struct_size;
    int32_t     width;
    int32_t     height;
    uint16_t    planes;
    uint16_t    bit_count;
    uint32_t    compression;
    uint32_t    image_size;
    int32_t     x_ppmeter;
    int32_t     y_ppmeter;
    uint32_t    color_used;
    uint32_t    important_color;
} bmp_header_t;
#pragma pack()

#define BM_HEADER_SIZE sizeof(bmp_header_t)

char http_post[] =
    "POST /upload HTTP/1.1\r\n"\
    "Host: binasc.tk\r\n"\
    "Connection: keep-alive\r\n"\
    "Content-Type: image/bmp\r\n"\
    "Content-Length: ";

char http_resp[] =
    "HTTP/1.1 200 OK\r\n"\
    "Server: binasc.tk\r\n"\
    "Connection: keep-alive\r\n"\
    "Content-Type: image/bmp\r\n"\
    "Content-Length: ";

char *http_enc(obscure_t *o, const char *http, size_t http_len,
               char *buf, size_t *len)
{
    size_t remain, to_copy;
    char *nbuf, *begin;
    int length_len;
    size_t content_len;
    bmp_header_t *bh;

    remain = *len;
    if (remain <= o->to_send_size) {
        o->to_send_size -= remain;
        return buf;
    }

    if (o->to_send_size > 0) {
        nbuf = enlarge_buffer(o, o->to_send_size);
        if (nbuf == NULL) {
            return NULL;
        }
        memcpy(nbuf, buf, o->to_send_size);
        buf += o->to_send_size;
        remain -= o->to_send_size;
    }
    o->remain.len = o->to_send_size;
    o->to_send_size = 0;

    content_len = 3 * 4 * 256;
    while (remain > 0) {
        to_copy = remain > content_len ? content_len : remain;
        nbuf = enlarge_buffer(o, o->remain.len
                              + http_len + 15
                              + BM_HEADER_SIZE + to_copy);
        if (nbuf == NULL) {
            return NULL;
        }
        nbuf = o->remain.buf + o->remain.len;
        begin = o->remain.buf + o->remain.len;

        memcpy(nbuf, http, http_len);
        nbuf += http_len;

        length_len = sprintf(nbuf, "%zu", BM_HEADER_SIZE + content_len);
        nbuf += length_len;

        memcpy(nbuf, "\r\n\r\n", 4);
        nbuf += 4;

        bh = (bmp_header_t *)nbuf;
        memcpy(nbuf, s_bh, BM_HEADER_SIZE);
        nbuf += BM_HEADER_SIZE;

        log_debug("encode new header: %zu", nbuf - begin);

        bh->size = BM_HEADER_SIZE + content_len;
        bh->height = content_len / 12;
        bh->image_size = content_len;

        memcpy(nbuf, buf, to_copy);
        buf += to_copy;
        nbuf += to_copy;
        remain -= to_copy;

        o->remain.len += nbuf - begin;
        o->to_send_size = content_len - to_copy;
    }

    *len = o->remain.len;
    return o->remain.buf;
}

int http_dec(obscure_t *o, const nl_buf_t *in, nl_buf_t *out)
{
    int i, j;
    size_t data_len, header_len;
    const char *eoh = "\r\n\r\n";
    bmp_header_t *bh;

    if (o->to_recv_size > 0) {
        data_len = in->len < o->to_recv_size ? in->len : o->to_recv_size;

        out->buf = in->buf;
        out->len = data_len;;
        o->to_recv_size -= data_len;
        return out->len;
    }

    for (i = 0, j = 0; i < in->len; i++) {
        for (j = 0; j < 4; j++) {
            if (i + j == in->len) {
                break;
            }
            else if (in->buf[i + j] != eoh[j]) {
                break;
            }
        }
        if (j == 4) {
            break;
        }
    }
    if (i + j == in->len || j < 4) {
        return 0;
    }

    header_len = i + j;
    if (in->len >= header_len + BM_HEADER_SIZE) {
        log_debug("decode new header: %zu", header_len + BM_HEADER_SIZE);
        bh = (bmp_header_t *)(in->buf + header_len);
        o->to_recv_size = bh->image_size;
        out->buf = NULL;
        out->len = 0;
        return header_len + BM_HEADER_SIZE;
    }

    return 0;
}

int acc_splitter(nl_connection_t *c, const nl_buf_t *in, nl_buf_t *out)
{
    socket_data_t *data = c->data;
    return http_dec(data->o, in, out);
}

int con_splitter(nl_connection_t *c, const nl_buf_t *in, nl_buf_t *out)
{
    socket_data_t *data = c->data;
    return http_dec(data->o, in, out);
}

void *acc_encode(obscure_t *o, void *buf, size_t *len)
{
    buf = xor(o, buf, len);
    buf = http_enc(o, http_resp, sizeof(http_resp) - 1, buf, len);
    return buf;
}

void *con_encode(obscure_t *o, void *buf, size_t *len)
{
    buf = xor(o, buf, len);
    buf = http_enc(o, http_post, sizeof(http_post) - 1, buf, len);
    return buf;
}

void *acc_decode(obscure_t *o, void *buf, size_t *len)
{
    buf = xor(o, buf, len);
    return buf;
}

void *con_decode(obscure_t *o, void *buf, size_t *len)
{
    buf = xor(o, buf, len);
    return buf;
}

void udp_obscure(udp_obscure_t *o)
{
    memset(o, 0, sizeof(udp_obscure_t));
}

char *udp_xor(udp_obscure_t *o, char *buf, size_t *len)
{
    size_t i;
    for (i = 0; i < *len; i++) {
        buf[i] = ~buf[i];
        buf[i] ^= s_key[i % 4];
    }

    return buf;
}

#define udp_safe_mtu (576 - 20 - 8)

char *udp_padding(udp_obscure_t *o, char *buf, size_t *len)
{
    unsigned char padding;

    padding = 1;
    if (*len < udp_safe_mtu) {
        padding = rand() % (udp_safe_mtu - *len) + 1;
    }

    memmove(buf + padding, buf, *len);
    *(unsigned char *)buf = padding;
    *len += padding;

    return buf;
}

char *udp_unpadding(udp_obscure_t *o, char *buf, size_t *len)
{
    unsigned char padding;

    padding = *(unsigned char *)buf;

    memmove(buf, buf + padding, *len - padding);
    *len -= padding;

    return buf;
}

void *udp_acc_encode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_padding(o, buf, len);
    buf = udp_xor(o, buf, len);
    return buf;
}

void *udp_con_encode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_padding(o, buf, len);
    buf = udp_xor(o, buf, len);
    return buf;
}

void *udp_acc_decode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_xor(o, buf, len);
    buf = udp_unpadding(o, buf, len);
    return buf;
}

void *udp_con_decode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_xor(o, buf, len);
    buf = udp_unpadding(o, buf, len);
    return buf;
}


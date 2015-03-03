#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "obscure.h"
#include "tunnel.h"
#include "sha1sum.h"
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
    o->id = -1;
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

int acc_splitter(nl_stream_t *c, const nl_buf_t *in, nl_buf_t *out)
{
    socket_data_t *data = c->data;
    return http_dec(data->o, in, out);
}

int con_splitter(nl_stream_t *c, const nl_buf_t *in, nl_buf_t *out)
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

#define least_padding 64
#define udp_safe_mtu (576 - 20 - 8 - 2 - 4)
#define random(min, max) (rand()%((max)-(min))+(min))

static void get_random_bytes(char *buf, size_t len)
{
    for ( ; len > 0; len--) {
        *(unsigned char *)(buf + len - 1) = rand() % 256;
    }
}

char *udp_padding(udp_obscure_t *o, char *buf, size_t *len)
{
    uint16_t padding;
    int min, max;
    SHA1_CONTEXT ctx;

    padding = 2;
    if (*len < udp_safe_mtu) {
        max = udp_safe_mtu - *len;
        min = least_padding < max ? least_padding : max;
        if (min < max) {
            padding = random(min, max + 1);
        }
        else {
            padding = min;
        }
        padding += 2;
    }

    memmove(buf + padding, buf, *len);
    *(uint16_t *)buf = padding;
    get_random_bytes(buf + 2, padding - 2);

    sha1_init(&ctx);
    sha1_write(&ctx, (unsigned char *)buf, padding + *len);
    sha1_final(&ctx);
    *(uint32_t *)(buf + padding + *len) = *(uint32_t *)(ctx.buf + 16);

    *len += padding + 4;

    return buf;
}

char *udp_unpadding(udp_obscure_t *o, char *buf, size_t *len)
{
    SHA1_CONTEXT ctx;
    uint16_t padding;

    padding = *(uint16_t *)buf;
    if (padding >= *len - 4) {
        log_error("corrupted packet");
        return NULL;
    }

    sha1_init(&ctx);
    sha1_write(&ctx, (unsigned char *)buf, *len - 4);
    sha1_final(&ctx);

    if (*(uint32_t *)(buf + *len - 4) != *(uint32_t *)(ctx.buf + 16)) {
        log_error("checksum failed");
        return NULL;
    }

    memmove(buf, buf + padding, *len - padding - 4);
    *len -= padding + 4;

    return buf;
}

typedef struct dns_header_s {
    uint16_t        id;
    uint16_t        flags;
    uint16_t        qdcount;
    uint16_t        ancount;
    uint16_t        nscount;
    uint16_t        arcount;
} dns_header_t;

typedef struct q_header_s {
    // qname
    uint16_t        qtype;
    uint16_t        qclass;
} q_header_t;

#pragma pack(1)
typedef struct rr_header_s {
    // name
    uint16_t        type;
    uint16_t        klass;
    uint32_t        ttl;
    uint16_t        rdlength;
} rr_header_t;
#pragma pack()

static char qname[] = "\01z\02cn";

char *udp_wrapper_dns_req(udp_obscure_t *o, char *buf, size_t *len)
{
    size_t inc;
    dns_header_t h;
    q_header_t qh;
    uint16_t offset;
    rr_header_t rrh;

    h.id = rand() % 0xffff;
    h.flags = htons(0);
    h.qdcount = htons(1);
    h.ancount = htons(0);
    h.nscount = htons(0);
    h.arcount = htons(1);

    qh.qtype = htons(1);
    qh.qclass = htons(1);

    offset = htons(0xc000 | (uint16_t) sizeof(h));
    rrh.type = htons(10);
    rrh.klass = htons(1);
    rrh.ttl = htonl(0x7fff);
    rrh.rdlength = htons(*len);

    memmove(buf + sizeof(h) + sizeof(qname) + sizeof(qh) + sizeof(offset) + sizeof(rrh), buf, *len);

    inc = 0;

    memcpy(buf + inc, &h, sizeof(h));
    inc += sizeof(h);

    memcpy(buf + inc, qname, sizeof(qname));
    inc += sizeof(qname);

    memcpy(buf + inc, &qh, sizeof(qh));
    inc += sizeof(qh);

    memcpy(buf + inc, &offset, sizeof(offset));
    inc += sizeof(offset);

    memcpy(buf + inc, &rrh, sizeof(rrh));
    inc += sizeof(rrh);

    *len += inc;

    return buf;
}

char *udp_wrapper_dns_res(udp_obscure_t *o, char *buf, size_t *len)
{
    size_t inc;
    dns_header_t h;
    rr_header_t ah;
    uint32_t addr;
    uint16_t offset;
    rr_header_t rrh;

    h.id = rand() % 0xffff;
    h.flags = htons(0x8000);
    h.qdcount = htons(0);
    h.ancount = htons(1);
    h.nscount = htons(0);
    h.arcount = htons(1);

    ah.type = htons(1);
    ah.klass = htons(1);
    ah.ttl = htonl(0x7fff);
    ah.rdlength = htons(4);

    addr = htonl(0x6a3210c6);

    offset = htons(0xc000 | (uint16_t) sizeof(h));
    rrh.type = htons(10);
    rrh.klass = htons(1);
    rrh.ttl = htonl(0x7fff);
    rrh.rdlength = htons(*len);

    memmove(buf + sizeof(h) + sizeof(qname) + sizeof(ah) + 4 + sizeof(offset) + sizeof(rrh), buf, *len);
    inc = 0;

    memcpy(buf + inc, &h, sizeof(h));
    inc += sizeof(h);

    memcpy(buf + inc, qname, sizeof(qname));
    inc += sizeof(qname);

    memcpy(buf + inc, &ah, sizeof(ah));
    inc += sizeof(ah);

    memcpy(buf + inc, &addr, 4);
    inc += 4;

    memcpy(buf + inc, &offset, sizeof(offset));
    inc += sizeof(offset);

    memcpy(buf + inc, &rrh, sizeof(rrh));
    inc += sizeof(rrh);

    *len += inc;

    return buf;
}

char *udp_unwrapper_dns(udp_obscure_t *o, void *buf, size_t *len)
{
    size_t skip;
    dns_header_t *dh;

    dh = buf;
    if (ntohs(dh->flags) & 0x8000) {
        // response
        skip = 12 + 6 + 10 + 4 + 2 + 10;
    }
    else {
        // req
        skip = 12 + 6 + 4 + 2 + 10;
    }
    if (*len <= skip) {
        return NULL;
    }
    *len -= skip;
    return buf + skip;
}

void *udp_acc_encode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_padding(o, buf, len);
    buf = udp_xor(o, buf, len);
    buf = udp_wrapper_dns_res(o, buf, len);
    return buf;
}

void *udp_con_encode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_padding(o, buf, len);
    buf = udp_xor(o, buf, len);
    buf = udp_wrapper_dns_req(o, buf, len);
    return buf;
}

void *udp_acc_decode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_unwrapper_dns(o, buf, len);
    buf = udp_xor(o, buf, len);
    buf = udp_unpadding(o, buf, len);
    return buf;
}

void *udp_con_decode(udp_obscure_t *o, void *buf, size_t *len)
{
    buf = udp_unwrapper_dns(o, buf, len);
    buf = udp_xor(o, buf, len);
    buf = udp_unpadding(o, buf, len);
    return buf;
}


#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "network.h"

int encode(char *inbuf, int inlen, char *outbuf);
int decode(char *inbuf, int inlen, char *outbuf);

static unsigned char s_key[4] = { 0x4a, 0x3f, 0xbc, 0x70 };

char *xor(char *buf, size_t *len)
{
    size_t i;
    for (i = 0; i < *len / 4; i++) {
        ((int32_t *)buf)[i] = ~((int32_t *)buf)[i];
        ((int32_t *)buf)[i] ^= *(int32_t *)s_key;
    }
    for (i = *len / 4; i < *len / 4 + *len % 4; i++) {
        buf[i] = ~buf[i];
        buf[i] ^= s_key[i % 4];
    }
    return buf;
}

char http_post[] =
    "POST /upload HTTP/1.1\r\n"\
    "Host: binasc.tk\r\n"\
    "Connection: keep-alive\r\n"\
    "Content-Type: image/bmp\r\n"\
    "Content-Length: ";

static unsigned char s_bmh[] = {
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

#define BM_HEADER_SIZE sizeof(s_bmh)
#define BM_HEADER_FSIZE_OFFSET 2
#define BM_HEADER_HEIGHT_OFFSET 22
#define BM_HEADER_CSIZE_OFFSET 34

char http_resp[] =
    "HTTP/1.1 200 OK\r\n"\
    "Server: binasc.tk\r\n"\
    "Connection: keep-alive\r\n"\
    "Content-Type: image/bmp\r\n"\
    "Content-Length: ";

char *http_enc(const char *http, size_t http_len,
               char *buf, size_t *len)
{
    char length[9];
    char *new_buf, *begin;
    size_t header_len;
    size_t content_len;
    content_len = ((*len + 1) / 12 + ((*len + 1) % 12 ? 1 : 0)) * 12;

    header_len = http_len;

    new_buf = malloc(header_len + 15 + BM_HEADER_SIZE + content_len);
    if (new_buf == NULL) {
        return NULL;
    }
    begin = new_buf;

    sprintf(length, "%lu", BM_HEADER_SIZE + content_len);

    memcpy(new_buf, http, http_len);
    new_buf += http_len;

    memcpy(new_buf, length, strlen(length));
    new_buf += strlen(length);

    memcpy(new_buf, "\r\n\r\n", 4);
    new_buf += 4;

    printf("encode: header: %zu, len: %zu\n", new_buf - begin, *len);

    memcpy(new_buf, s_bmh, BM_HEADER_SIZE);
    *(int32_t *)(new_buf + BM_HEADER_FSIZE_OFFSET) = BM_HEADER_SIZE + content_len;
    *(int32_t *)(new_buf + BM_HEADER_HEIGHT_OFFSET) = content_len / 12; 
    *(int32_t *)(new_buf + BM_HEADER_CSIZE_OFFSET) = content_len;
    new_buf += BM_HEADER_SIZE;

    memcpy(new_buf, buf, *len);
    new_buf += content_len;

    if ((*len + 1) % 12 == 0) {
        *(unsigned char *)(new_buf - 1) = 1;
    }
    else {
        *(unsigned char *)(new_buf - 1) = 12 - ((*len + 1) % 12) + 1;
    }
    printf("padding: %u\n", *(unsigned char *)(new_buf - 1));

    *len = new_buf - begin;

    //free(buf);

    return begin;
}

int http_dec(const nl_buf_t *in, nl_buf_t *out)
{
    int i, j;
    size_t header_len;
    const char *eoh = "\r\n\r\n";
    size_t content_len;
    char *bmh;
    unsigned char padding;

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
        bmh = in->buf + header_len;
        content_len = *(int32_t *)(bmh + BM_HEADER_CSIZE_OFFSET);
        if (in->len >= header_len + BM_HEADER_SIZE + content_len) {
            out->buf = bmh +  BM_HEADER_SIZE;
            padding = *(unsigned char *)(out->buf + content_len - 1);
            out->len = content_len - padding;
            printf("decode: header: %zu, len: %zu\n", header_len, out->len);
            printf("padding: %u\n", padding);
            return header_len + BM_HEADER_SIZE + content_len;
        }
    }

    return 0;
}

int acc_splitter(const nl_buf_t *in, nl_buf_t *out)
{
    return http_dec(in, out);
}

int con_splitter(const nl_buf_t *in, nl_buf_t *out)
{
    return http_dec(in, out);
}

void *acc_encode(void *buf, size_t *len)
{
    buf = xor(buf, len);
    buf = http_enc(http_resp, sizeof(http_resp) - 1, buf, len);
    return buf;
}

void *con_encode(void *buf, size_t *len)
{
    buf = xor(buf, len);
    buf = http_enc(http_post, sizeof(http_post) - 1, buf, len);
    return buf;
}

void *acc_decode(void *buf, size_t *len)
{
    buf = xor(buf, len);
    return buf;
}

void *con_decode(void *buf, size_t *len)
{
    buf = xor(buf, len);
    return buf;
}


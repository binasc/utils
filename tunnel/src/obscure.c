#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "network.h"

char *xor(char *buf, size_t *len)
{
    size_t i;
    for (i = 0; i < *len; i++) {
        buf[i] = ~buf[i];
    }
    return buf;
}

char http_post[] =
    "POST / HTTP/1.1\r\n"\
    "Host: home.binasc.tk\r\n"\
    "Content-Type: application/vnd.ms-excel\r\n"\
    "Content-Length:       \r\n\r\n";

char http_resp[] =
    "HTTP/1.1 200 OK\r\n"\
    "Server: home.binasc.tk\r\n"\
    "Content-Type: application/vnd.ms-excel\r\n"\
    "Content-Length:       \r\n\r\n";

char *http_enc(const char *http, size_t http_len,
               char *buf, size_t *len)
{
    char length[8];
    char *new_buf;
    size_t header_len;

    int16_t l = *len;

    header_len = http_len + sizeof(l);

    new_buf = malloc(header_len + l);
    if (new_buf == NULL) {
        return NULL;
    }
    sprintf(length, "%d", l);
    memcpy(new_buf, http, http_len);
    memcpy(new_buf + http_len - 8, length, strlen(length));
    memcpy(new_buf + http_len, &l, sizeof(l));
    memcpy(new_buf + header_len, buf, l);

    *len = header_len + l;

    printf("encode: header: %zu, len: %d\n", header_len, l);
    //free(buf);

    return new_buf;
}

int http_dec(const char *http, size_t http_len,
             const nl_buf_t *in, nl_buf_t *out)
{
    int16_t len;
    size_t header_len;

    header_len = http_len + sizeof(int16_t);
    if (in->len >= header_len) {
        len = *(int16_t *)(in->buf + http_len);
        if (in->len >= header_len + len) {
            out->buf = in->buf + header_len;
            out->len = len;
            printf("decode: header: %zu, len: %d\n", header_len, len);
            return header_len + len;
        }
    }

    return 0;
}

int acc_splitter(const nl_buf_t *in, nl_buf_t *out)
{
    return http_dec(http_post, sizeof(http_post) - 1, in, out);
}

int con_splitter(const nl_buf_t *in, nl_buf_t *out)
{
    return http_dec(http_resp, sizeof(http_resp) - 1, in, out);
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


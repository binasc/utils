#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "obscure.h"
#include "log.h"

//void udp_obscure(udp_obscure_t *o)
//{
//    memset(o, 0, sizeof(udp_obscure_t));
//}
//
//char *udp_xor(udp_obscure_t *o, char *buf, size_t *len)
//{
//    size_t i;
//    for (i = 0; i < *len; i++) {
//        buf[i] = ~buf[i];
//        buf[i] ^= s_key[i % 4];
//    }
//
//    return buf;
//}
//
//#define least_padding 64
//#define udp_safe_mtu (576 - 20 - 8 - 2 - 4)
//#define random(min, max) (rand()%((max)-(min))+(min))
//
//static void get_random_bytes(char *buf, size_t len)
//{
//    for ( ; len > 0; len--) {
//        *(unsigned char *)(buf + len - 1) = rand() % 256;
//    }
//}
//
//char *udp_padding(udp_obscure_t *o, char *buf, size_t *len)
//{
//    uint16_t padding;
//    int min, max;
//    SHA1_CONTEXT ctx;
//
//    padding = 2;
//    if (*len < udp_safe_mtu) {
//        max = udp_safe_mtu - *len;
//        min = least_padding < max ? least_padding : max;
//        if (min < max) {
//            padding = random(min, max + 1);
//        }
//        else {
//            padding = min;
//        }
//        padding += 2;
//    }
//
//    memmove(buf + padding, buf, *len);
//    *(uint16_t *)buf = padding;
//    get_random_bytes(buf + 2, padding - 2);
//
//    sha1_init(&ctx);
//    sha1_write(&ctx, (unsigned char *)buf, padding + *len);
//    sha1_final(&ctx);
//    *(uint32_t *)(buf + padding + *len) = *(uint32_t *)(ctx.buf + 16);
//
//    *len += padding + 4;
//
//    return buf;
//}
//
//char *udp_unpadding(udp_obscure_t *o, char *buf, size_t *len)
//{
//    SHA1_CONTEXT ctx;
//    uint16_t padding;
//
//    padding = *(uint16_t *)buf;
//    if (padding >= *len - 4) {
//        log_error("corrupted packet");
//        return NULL;
//    }
//
//    sha1_init(&ctx);
//    sha1_write(&ctx, (unsigned char *)buf, *len - 4);
//    sha1_final(&ctx);
//
//    if (*(uint32_t *)(buf + *len - 4) != *(uint32_t *)(ctx.buf + 16)) {
//        log_error("checksum failed");
//        return NULL;
//    }
//
//    memmove(buf, buf + padding, *len - padding - 4);
//    *len -= padding + 4;
//
//    return buf;
//}

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

//static char qname[] = "\01z\02cn";
//
//char *udp_wrapper_dns_req(udp_obscure_t *o, char *buf, size_t *len)
//{
//    size_t inc;
//    dns_header_t h;
//    q_header_t qh;
//    uint16_t offset;
//    rr_header_t rrh;
//
//    h.id = rand() % 0xffff;
//    h.flags = htons(0);
//    h.qdcount = htons(1);
//    h.ancount = htons(0);
//    h.nscount = htons(0);
//    h.arcount = htons(1);
//
//    qh.qtype = htons(1);
//    qh.qclass = htons(1);
//
//    offset = htons(0xc000 | (uint16_t) sizeof(h));
//    rrh.type = htons(10);
//    rrh.klass = htons(1);
//    rrh.ttl = htonl(0x7fff);
//    rrh.rdlength = htons(*len);
//
//    memmove(buf + sizeof(h) + sizeof(qname) + sizeof(qh) + sizeof(offset) + sizeof(rrh), buf, *len);
//
//    inc = 0;
//
//    memcpy(buf + inc, &h, sizeof(h));
//    inc += sizeof(h);
//
//    memcpy(buf + inc, qname, sizeof(qname));
//    inc += sizeof(qname);
//
//    memcpy(buf + inc, &qh, sizeof(qh));
//    inc += sizeof(qh);
//
//    memcpy(buf + inc, &offset, sizeof(offset));
//    inc += sizeof(offset);
//
//    memcpy(buf + inc, &rrh, sizeof(rrh));
//    inc += sizeof(rrh);
//
//    *len += inc;
//
//    return buf;
//}
//
//char *udp_wrapper_dns_res(udp_obscure_t *o, char *buf, size_t *len)
//{
//    size_t inc;
//    dns_header_t h;
//    rr_header_t ah;
//    uint32_t addr;
//    uint16_t offset;
//    rr_header_t rrh;
//
//    h.id = rand() % 0xffff;
//    h.flags = htons(0x8000);
//    h.qdcount = htons(0);
//    h.ancount = htons(1);
//    h.nscount = htons(0);
//    h.arcount = htons(1);
//
//    ah.type = htons(1);
//    ah.klass = htons(1);
//    ah.ttl = htonl(0x7fff);
//    ah.rdlength = htons(4);
//
//    addr = htonl(0x6a3210c6);
//
//    offset = htons(0xc000 | (uint16_t) sizeof(h));
//    rrh.type = htons(10);
//    rrh.klass = htons(1);
//    rrh.ttl = htonl(0x7fff);
//    rrh.rdlength = htons(*len);
//
//    memmove(buf + sizeof(h) + sizeof(qname) + sizeof(ah) + 4 + sizeof(offset) + sizeof(rrh), buf, *len);
//    inc = 0;
//
//    memcpy(buf + inc, &h, sizeof(h));
//    inc += sizeof(h);
//
//    memcpy(buf + inc, qname, sizeof(qname));
//    inc += sizeof(qname);
//
//    memcpy(buf + inc, &ah, sizeof(ah));
//    inc += sizeof(ah);
//
//    memcpy(buf + inc, &addr, 4);
//    inc += 4;
//
//    memcpy(buf + inc, &offset, sizeof(offset));
//    inc += sizeof(offset);
//
//    memcpy(buf + inc, &rrh, sizeof(rrh));
//    inc += sizeof(rrh);
//
//    *len += inc;
//
//    return buf;
//}
//
//char *udp_unwrapper_dns(udp_obscure_t *o, void *buf, size_t *len)
//{
//    size_t skip;
//    dns_header_t *dh;
//
//    dh = buf;
//    if (ntohs(dh->flags) & 0x8000) {
//        // response
//        skip = 12 + 6 + 10 + 4 + 2 + 10;
//    }
//    else {
//        // req
//        skip = 12 + 6 + 4 + 2 + 10;
//    }
//    if (*len <= skip) {
//        return NULL;
//    }
//    *len -= skip;
//    return buf + skip;
//}
//
//void *udp_acc_encode(udp_obscure_t *o, void *buf, size_t *len)
//{
//    buf = udp_padding(o, buf, len);
//    buf = udp_xor(o, buf, len);
//    buf = udp_wrapper_dns_res(o, buf, len);
//    return buf;
//}
//
//void *udp_con_encode(udp_obscure_t *o, void *buf, size_t *len)
//{
//    buf = udp_padding(o, buf, len);
//    buf = udp_xor(o, buf, len);
//    buf = udp_wrapper_dns_req(o, buf, len);
//    return buf;
//}
//
//void *udp_acc_decode(udp_obscure_t *o, void *buf, size_t *len)
//{
//    buf = udp_unwrapper_dns(o, buf, len);
//    buf = udp_xor(o, buf, len);
//    buf = udp_unpadding(o, buf, len);
//    return buf;
//}
//
//void *udp_con_decode(udp_obscure_t *o, void *buf, size_t *len)
//{
//    buf = udp_unwrapper_dns(o, buf, len);
//    buf = udp_xor(o, buf, len);
//    buf = udp_unpadding(o, buf, len);
//    return buf;
//}
//

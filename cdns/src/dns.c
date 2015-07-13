#include "dns.h"
#include "log.h"

#include <string.h>
#include <netinet/in.h>

#define DNS_OFFSET_ID       0
#define DNS_OFFSET_FLAGS    2
#define DNS_OFFSET_QDCOUNT  4
#define DNS_OFFSET_ANCOUNT  6
#define DNS_OFFSET_NSCOUNT  8
#define DNS_OFFSET_ARCOUNT  10
#define DNS_OFFSET_FIRST_RR 12

static void dmsg_debug_print_name(const dmsg_t *d, const char *name)
{
    while (*name != 0) {
        if (((*(uint8_t *)name >> 6) & 0x3) == 0) {
            printf("%.*s.", *(uint8_t *)name, name + 1);
            name += *name + 1;
        }
        else {
            dmsg_debug_print_name(d, d->buf + (ntohs(*(uint16_t *)name) & 0x3fff));
            break;
        }
    }
}

static void dmsg_debug_print_qd(const dmsg_t *d, const char *qd)
{
    dmsg_debug_print_name(d, qd);
    puts("");
    printf("qtype: %hu\n", dmsg_get_qtype(d, qd));
    printf("qclass: %hu\n", dmsg_get_qclass(d, qd));
}

static void dmsg_debug_print_rr(const dmsg_t *d, const char *rr)
{
    dmsg_debug_print_name(d, rr);
    puts("");
    printf("type: %hu\n", dmsg_get_type(d, rr));
    printf("class: %hu\n", dmsg_get_class(d, rr));
    printf("ttl: %u\n", dmsg_get_ttl(d, rr));
    printf("rdlength: %hu\n", dmsg_get_rdlength(d,rr));
}

void dmsg_debug_print(const dmsg_t *d)
{
    int i, count;

    printf("id: %hu\n", dmsg_get_id(d));
    printf("qr: %hu\n", dmsg_get_qr(d));
    printf("opcode: %hu\n", dmsg_get_opcode(d));
    printf("aa: %hu\n", dmsg_get_aa(d));
    printf("tc: %hu\n", dmsg_get_tc(d));
    printf("rd: %hu\n", dmsg_get_rd(d));
    printf("qdcount: %d\n", dmsg_get_qdcount(d));
    printf("ancount: %d\n", dmsg_get_ancount(d));
    printf("nscount: %d\n", dmsg_get_nscount(d));
    printf("arcount: %d\n", dmsg_get_arcount(d));

    count = 0;
    printf("QUESTION:\n");
    for (i = 0; i < dmsg_get_qdcount(d); i++) {
        dmsg_debug_print_qd(d, d->rr[i + count]);
    }
    count += dmsg_get_qdcount(d);
    printf("ANSWER:\n");
    for (i = 0; i < dmsg_get_ancount(d); i++) {
        dmsg_debug_print_rr(d, d->rr[i + count]);
    }
    count += dmsg_get_ancount(d);
    printf("AUTHORITY:\n");
    for (i = 0; i < dmsg_get_nscount(d); i++) {
        dmsg_debug_print_rr(d, d->rr[i + count]);
    }
    count += dmsg_get_nscount(d);
    printf("ADDITIONAL:\n");
    for (i = 0; i < dmsg_get_arcount(d); i++) {
        dmsg_debug_print_rr(d, d->rr[i + count]);
    }
}

int dmsg_new(dmsg_t *d, const char *buf, size_t len)
{
    int i, count;
    char *pos;

    d->buf = malloc(len);
    if (d->buf == NULL) {
        log_error("malloc() failed");
        return -1;
    }
    memcpy(d->buf, buf, len);

    count = 0;
    count += dmsg_get_qdcount(d);
    count += dmsg_get_ancount(d);
    count += dmsg_get_nscount(d);
    count += dmsg_get_arcount(d);

    if (count == 0) {
        log_error("empty dns message");
        free(d->buf);
        return -1;
    }
    d->rr = malloc(count * sizeof(char *));
    if (d->rr == NULL) {
        log_error("malloc() failed");
        free(d->buf);
        return -1;
    }
    pos = d->buf + DNS_OFFSET_FIRST_RR;
    for (i = 0; i < count; i++) {
        int break_from_point = 0;
        d->rr[i] = pos;
        while (pos - d->buf < len && *pos != 0) {
            if (((*(uint8_t *)pos >> 6) & 0x3) == 0) {
                pos += *pos + 1;
            }
            else {
                pos += 2;
                break_from_point = 1;
                break;
            }
        }
        if (pos - d->buf < len && !break_from_point) {
            pos += 1;
        }

        if (i < dmsg_get_qdcount(d)) {
            pos += 4;
        }
        else {
            pos += 10;
            if (pos - d->buf > len) {
                break;
            }
            pos += ntohs(*(uint16_t *)(pos - 2));
        }
    }

    if (pos - d->buf != len) {
        log_error("failed to resolve dns message");
        free(d->buf);
        free(d->rr);
        return -1;
    }

    return 0;
}

void dmsg_delete(dmsg_t *d)
{
    free(d->buf);
    free(d->rr);
}

uint16_t dmsg_get_id(const dmsg_t *d)
{
    return *(uint16_t *)(d->buf + DNS_OFFSET_ID);
}

uint16_t dmsg_get_qr(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS) >> 7) & 0x1;
}

uint16_t dmsg_get_opcode(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS) >> 3) & 0xf;
}

uint16_t dmsg_get_aa(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS) >> 2) & 0x1;
}

uint16_t dmsg_get_tc(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS) >> 1) & 0x1;
}

uint16_t dmsg_get_rd(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS) >> 0) & 0x1;
}

uint16_t dmsg_get_ra(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS + 1) >> 7) & 0x1;
}

uint16_t dmsg_get_z(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS + 1) >> 4) & 0x7;
}

uint16_t dmsg_get_rcode(const dmsg_t *d)
{
    return (*(uint8_t *)(d->buf + DNS_OFFSET_FLAGS + 1) >> 0) & 0xf;
}

uint16_t dmsg_get_qdcount(const dmsg_t *d)
{
    return ntohs(*(uint16_t *)(d->buf + DNS_OFFSET_QDCOUNT));
}

uint16_t dmsg_get_ancount(const dmsg_t *d)
{
    return ntohs(*(uint16_t *)(d->buf + DNS_OFFSET_ANCOUNT));
}

uint16_t dmsg_get_nscount(const dmsg_t *d)
{
    return ntohs(*(uint16_t *)(d->buf + DNS_OFFSET_NSCOUNT));
}

uint16_t dmsg_get_arcount(const dmsg_t *d)
{
    return ntohs(*(uint16_t *)(d->buf + DNS_OFFSET_ARCOUNT));
}

static const char *dmsg_skip_name(const dmsg_t *d, const char *rr)
{
    while (*rr != 0) {
        if (((*(uint8_t *)rr >> 6) & 0x3) == 0) {
            rr += *rr + 1;
        }
        else {
            dmsg_skip_name(d, d->buf + (ntohs(*(uint16_t *)rr) & 0x3fff));
            return rr + 2;
        }
    }
    return rr + 1;
}

static int dmsg_fill_name(const dmsg_t *d, const char *rr, char *buf)
{
    while (*rr != 0) {
        if (((*(uint8_t *)rr >> 6) & 0x3) == 0) {
            memcpy(buf, rr + 1, *rr);
            buf[*(uint8_t *)rr] = '.';
            buf += *rr + 1;
            rr += *rr + 1;
        }
        else {
            int rc;
            rc = dmsg_fill_name(d, d->buf + (ntohs(*(uint16_t *)rr) & 0x3fff), buf);
            return buf - d->buf + rc;
        }
    }
    *buf = 0;
    return buf - d->buf;
}

dmsg_qd dmsg_get_qd(const dmsg_t *d, int pos)
{
    return d->rr[pos];
}

int dmsg_get_qname(const dmsg_t *d, const_dmsg_qd qd, char *name)
{
    return dmsg_fill_name(d, qd, name);
}

uint16_t dmsg_get_qtype(const dmsg_t *d, const_dmsg_qd qd)
{
    qd = dmsg_skip_name(d, qd);
    return ntohs(*(uint16_t *)(qd + 0));
}

dmsg_rr dmsg_get_an(const dmsg_t *d, int pos)
{
    pos += dmsg_get_qdcount(d);
    return d->rr[pos];
}

uint16_t dmsg_get_qclass(const dmsg_t *d, const_dmsg_qd qd)
{
    qd = dmsg_skip_name(d, qd);
    return ntohs(*(uint16_t *)(qd + 2));
}

int dmsg_get_name(const dmsg_t *d, const_dmsg_rr rr, char *name)
{
    return dmsg_fill_name(d, rr, name);
}

uint16_t dmsg_get_type(const dmsg_t *d, const_dmsg_rr rr)
{
    rr = dmsg_skip_name(d, rr);
    return ntohs(*(uint16_t *)(rr + 0));
}

uint16_t dmsg_get_class(const dmsg_t *d, const_dmsg_rr rr)
{
    rr = dmsg_skip_name(d, rr);
    return ntohs(*(uint16_t *)(rr + 2));
}

uint32_t dmsg_get_ttl(const dmsg_t *d, const_dmsg_rr rr)
{
    rr = dmsg_skip_name(d, rr);
    return ntohs(*(uint16_t *)(rr + 4));
}

uint16_t dmsg_get_rdlength(const dmsg_t *d, const_dmsg_rr rr)
{
    rr = dmsg_skip_name(d, rr);
    return ntohs(*(uint16_t *)(rr + 8));
}

const void *dmsg_get_rdata(const dmsg_t *d, const_dmsg_rr rr)
{
    rr = dmsg_skip_name(d, rr);
    return rr + 10;
}


#ifndef __DNS_H__
#define __DNS_H__

#include <stdlib.h>

typedef char * dmsg_qd;
typedef char * dmsg_rr;
typedef const char * const_dmsg_qd;
typedef const char * const_dmsg_rr;

typedef struct dmsg_s {
    char        *buf;
    char        **rr;
} dmsg_t;

int dmsg_new(dmsg_t *d, const char *buf, size_t len);
uint16_t dmsg_get_id(const dmsg_t *d);
uint16_t dmsg_get_qr(const dmsg_t *d);
uint16_t dmsg_get_opcode(const dmsg_t *d);
uint16_t dmsg_get_aa(const dmsg_t *d);
uint16_t dmsg_get_tc(const dmsg_t *d);
uint16_t dmsg_get_rd(const dmsg_t *d);
uint16_t dmsg_get_ra(const dmsg_t *d);
uint16_t dmsg_get_z(const dmsg_t *d);
uint16_t dmsg_get_rcode(const dmsg_t *d);
uint16_t dmsg_get_qdcount(const dmsg_t *d);
uint16_t dmsg_get_ancount(const dmsg_t *d);
uint16_t dmsg_get_nscount(const dmsg_t *d);
uint16_t dmsg_get_arcount(const dmsg_t *d);

dmsg_qd dmsg_get_qd(const dmsg_t *d, int pos);
uint16_t dmsg_get_qtype(const dmsg_t *d, const_dmsg_qd qd);
uint16_t dmsg_get_qclass(const dmsg_t *d, const_dmsg_qd qd);

dmsg_rr dmsg_get_rr(const dmsg_t *d, int pos);
uint16_t dmsg_get_type(const dmsg_t *d, const_dmsg_rr rr);
uint16_t dmsg_get_class(const dmsg_t *d, const_dmsg_rr rr);
uint32_t dmsg_get_ttl(const dmsg_t *d, const_dmsg_rr rr);
uint16_t dmsg_get_rdlength(const dmsg_t *d, const_dmsg_rr rr);

void dmsg_debug_print(const dmsg_t *d);

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

#endif


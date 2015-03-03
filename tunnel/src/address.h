#ifndef __ADDRESS_H__
#define __ADDRESS_H__

#include <arpa/inet.h>

int nl_resolve(const char *name, struct sockaddr *addr);

typedef struct nl_address_s
{
    int         af;

    const char  *name;

    /* for tcp & ucp */
    uint16_t    port;

    union {
        struct sockaddr_in inet4;
    };
} nl_address_t;

int nl_address_setname(nl_address_t *addr, const char *name);
int nl_address_setport(nl_address_t *addr, uint16_t port);

int nl_address_setinet4addr(nl_address_t *addr, struct sockaddr_in *in);
int nl_address_getinet4addr(nl_address_t *addr, struct sockaddr_in *in);

int nl_address_setsockaddr(nl_address_t *addr, struct sockaddr *saddr);
int nl_address_getsockaddr(nl_address_t *addr, struct sockaddr *saddr);

#endif


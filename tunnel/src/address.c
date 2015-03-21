#include "address.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int nl_address_setname(nl_address_t *addr, const char *name)
{
    addr->name = name;

    return 0;
}

int nl_address_setport(nl_address_t *addr, uint16_t port)
{
    addr->port = port;

    return 0;
}

const char *nl_address_getname(nl_address_t *addr)
{
    return addr->name;
}

uint16_t nl_address_getport(nl_address_t *addr)
{
    return addr->port;
}

int nl_address_setinet4addr(nl_address_t *addr, struct sockaddr_in *in)
{
    addr->name = NULL;
    addr->af = AF_INET;
    memcpy(&addr->inet4, in, sizeof(addr->inet4));

    return 0;
}

int nl_address_getinet4addr(nl_address_t *addr, struct sockaddr_in *in)
{
    int rc;

    if (addr->name != NULL) {
        in->sin_family = AF_INET;
        rc = nl_resolve(addr->name, (struct sockaddr *)in);
        if (rc == -1) {
            return -1;
        }
        in->sin_port = htons(addr->port);
    }
    else {
        memcpy(in, &addr->inet4, sizeof(addr->inet4));
    }

    return 0;
}

int nl_address_setsockaddr(nl_address_t *addr, struct sockaddr *saddr)
{
    addr->name = NULL;
    addr->af = saddr->sa_family;

    switch (addr->af) {
    case AF_INET:
        return nl_address_setinet4addr(addr, (struct sockaddr_in *)saddr);
        break;
    default:
        log_error("unsupported address family: %d", addr->af);
        return -1;
    }
}

int nl_address_getsockaddr(nl_address_t *addr, struct sockaddr *saddr)
{
    if (addr->name != NULL) {
        // TODO: which protocol preferred?
        return nl_address_getinet4addr(addr, (struct sockaddr_in *)saddr);
    }

    switch (addr->af) {
    case AF_INET:
        return nl_address_getinet4addr(addr, (struct sockaddr_in *)saddr);
    default:
        log_error("unsupported address family: %d", addr->af);
        return -1;
    }

    return 0;
}

int nl_resolve(const char *name, struct sockaddr *addr)
{
    int rc;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    /* AF_UNSPEC, AF_INET, AF_INET6 */
    hints.ai_family = addr->sa_family;
    hints.ai_socktype = 0;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(name, NULL, &hints, &result);
    if (rc == -1) {
        log_error("getaddrinfo: %s", gai_strerror(rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        /* dns load balance is not handled here */
        memcpy(addr, rp->ai_addr, rp->ai_addrlen);
        freeaddrinfo(result);
        return 0;
    }

    freeaddrinfo(result);
    log_error("cann't resolve %s", name);
    return -1;
}


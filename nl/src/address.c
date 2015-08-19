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
        log_error("nl_address_setsockaddr: unsupported address family: %d", addr->af);
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
        log_error("nl_address_getsockaddr: unsupported address family: %d", addr->af);
        return -1;
    }

    return 0;
}

int nl_resolve(const char *name, struct sockaddr *addr)
{
    int rc;
    struct in_addr in_addr;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    rc = inet_pton(AF_INET, name, &in_addr);
    if (rc == 1) {
        ((struct sockaddr_in *)addr)->sin_addr = in_addr;
        return 0;
    }

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
    if (rc != 0) {
        log_error("getaddrinfo: %d(%s)", rc, gai_strerror(rc));
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

static int inet46_ntop(struct sockaddr *addr, char *dst)
{
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)addr;
        if (inet_ntop(AF_INET, &in->sin_addr, dst, INET_ADDRSTRLEN) == NULL) {
            return -1;
        }
        sprintf(dst, "%s:%d", dst, ntohs(in->sin_port));

        return 0;
    }
    else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr;
        if (inet_ntop(AF_INET6, &in6->sin6_addr, dst, INET6_ADDRSTRLEN) == NULL) {
            return -1;
        }
        sprintf(dst, "%s:%d", dst, ntohs(in6->sin6_port));

        return 0;
    }

    log_error("inet46_ntop: unsupported address family: %d", addr->sa_family);
    return -1;
}

const char *nl_address_tostring(nl_address_t *addr)
{
    // TODO:
    static char straddr[INET6_ADDRSTRLEN + 6];
    static struct sockaddr saddr;

    if (nl_address_getsockaddr(addr, &saddr) < 0) {
        return NULL;
    }

    if (inet46_ntop(&saddr, straddr) == -1) {
        return NULL;
    }

    return straddr;
}


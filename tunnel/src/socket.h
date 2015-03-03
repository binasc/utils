#ifndef __SOCKET_H__
#define __SOCKET_H__

#include "address.h"
#include "event.h"

#define NL_STREAM   0
#define NL_DGRAM    1

typedef struct nl_socket_s
{
    nl_address_t        addr;
    int                 err;
    int                 fd;
    unsigned            type :3;
    unsigned            open :1;
    unsigned            error :1;
    unsigned            connected :1;

    nl_event_t          rev;
    nl_event_t          wev;

    handler_fn          chandler;

    void                *data;
} nl_socket_t;

int nl_socket(nl_socket_t *sock, int type);
int nl_accept(nl_socket_t *sock, nl_socket_t *nsock);
int nl_bind(nl_socket_t *sock, nl_address_t *addr);
int nl_listen(nl_socket_t *sock, int backlog);
int nl_connect(nl_socket_t *sock, nl_address_t *addr);
int nl_recv(nl_socket_t *sock, char *buf, size_t len);
int nl_recvfrom(nl_socket_t *sock, char *buf, size_t len, nl_address_t *addr);
int nl_send(nl_socket_t *sock, const char *buf, size_t len);
int nl_sendto(nl_socket_t *sock, const char *buf, size_t len, nl_address_t *addr);
int nl_close(nl_socket_t *sock);

void nl_socket_copy(nl_socket_t *dst, nl_socket_t *src);

#endif


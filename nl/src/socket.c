#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "socket.h"
#include "log.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

static void nl_socket_init(nl_socket_t *sock)
{
    memset(sock, 0, sizeof(nl_socket_t));

    sock->wev.index = NL_INVALID_INDEX;
    sock->wev.write = 1;
    sock->wev.data = sock;
    sock->rev.index = NL_INVALID_INDEX;
    sock->rev.write = 0;
    sock->rev.data = sock;
}

int nl_socket(nl_socket_t *sock, int type)
{
    int fd, sock_type, flags;

    sock->error = 0;
    sock->err = 0;

    switch (type) {
    case NL_STREAM:
        sock_type = SOCK_STREAM;
        break;
    case NL_DGRAM:
        sock_type = SOCK_DGRAM;
        break;
    default:
        sock->error = 1;
        log_error("#- unsupported type: %d", type);
        return -1;
    }

    // TODO: support PF_INET6
    fd = socket(PF_INET, sock_type, 0);
    if (fd == -1) {
        sock->err = errno;
        sock->error = 1;
        log_error("#- socket: %s", strerror(sock->err));
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        sock->err = errno;
        sock->error = 1;
        log_error("#%d fcntl F_GETFL: %s", fd, strerror(sock->err));
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        sock->err = errno;
        sock->error = 1;
        log_error("#%d fcntl F_SETFL: %s", fd, strerror(sock->err));
        close(fd);
        return -1;
    }

    nl_socket_init(sock);
    sock->fd = fd;
    sock->open = 1;
    sock->type = type;

    log_trace("#%d created", fd);

    return 0;
}

int nl_accept(nl_socket_t *sock, nl_socket_t *nsock)
{
    int                 fd, err, flags;
    struct sockaddr_in  addr;
    socklen_t           size;

    sock->error = 0;
    sock->err = 0;
    nsock->error = 0;
    nsock->err = 0;

    for ( ; ; ) {
        size = sizeof(struct sockaddr_in);
        fd = accept(sock->fd, (struct sockaddr *)&addr, &size);
        if (fd < 0) {
            err = errno;
            if (err == ECONNABORTED) {
                continue;
            }
            else if (err != EAGAIN && err != EWOULDBLOCK) {
                sock->error = 1;
                sock->err = err;
                log_error("#%d accept: %s", sock->fd, strerror(sock->err));
            }
            return -1;
        }
        else {
            break;
        }
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        nsock->error = 1;
        nsock->err = errno;
        log_error("#%d fcntl F_GETFL: %s", fd, strerror(nsock->err));
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        nsock->error = 1;
        nsock->err = errno;
        log_error("#%d fcntl F_SETFL: %s", fd, strerror(nsock->err));
        close(fd);
        return -1;
    }

    nl_socket_init(nsock);
    nsock->type = NL_STREAM;
    nsock->fd = fd;
    nsock->open = 1;
    nsock->connected = 1;

    log_trace("#%d accept #%d", sock->fd, fd);

    return 0;
}

int nl_bind(nl_socket_t *sock, nl_address_t *addr)
{
    int rc, on;
    struct sockaddr saddr;

    sock->error = 0;
    sock->err = 0;

    on = 1;
    rc = setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR,
                    (const void *)&on , sizeof(int));
    if (rc == -1) {
        sock->error = 1;
        sock->err = errno;
        log_error("#%d setsockopt: %s", sock->fd, strerror(sock->err));
        return -1;
    }

    rc = nl_address_getsockaddr(addr, &saddr);
    if (rc == -1) {
        sock->error = 1;
        return -1;
    }

    rc = bind(sock->fd, &saddr, sizeof(saddr));
    if (rc == -1) {
        sock->error = 1;
        sock->err = errno;
        log_error("#%d bind: %s", sock->fd, strerror(sock->err));
        return -1;
    }

    log_trace("#%d bound", sock->fd);

    return 0;
}

int nl_listen(nl_socket_t *sock, int backlog)
{
    int rc;

    sock->error = 0;
    sock->err = 0;

    rc = listen(sock->fd, backlog);
    if (rc < 0) {
        sock->error = 1;
        sock->err = errno;
        log_error("#%d listen: %s", sock->fd, strerror(sock->err));
        return -1;
    }

    sock->connected = 1;

    nl_event_add(&sock->rev);

    log_trace("#%d listening", sock->fd);

    return 0;
}

static int nl_post_connect(nl_socket_t *sock)
{
    int         rc, err;
    socklen_t   len;

    sock->error = 0;
    sock->err = 0;

    len = sizeof(err);
    rc = getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (rc == -1) {
        sock->error = 1;
        sock->err = errno;
        log_error("#%d getsockopt: %s", sock->fd, strerror(sock->err));
        return -1;
    }
    else if (err != 0) {
        sock->error = 1;
        sock->err = err;
        log_error("#%d connect: so_error: %d", sock->fd, err);
        return -1;
    }

    sock->connected = 1;
    log_trace("#%d connected", sock->fd);

    return 0;
}

static void nl_connect_wrapper(nl_event_t *ev)
{
    nl_socket_t    *sock;

    sock = ev->data;
    log_trace("#%d nl_connect_wrapper", sock->fd);

    (void) nl_post_connect(sock);
    ev->handler = sock->chandler;

    ev->handler(ev);
}

int nl_connect(nl_socket_t *sock, nl_address_t *addr)
{
    int rc;
    struct sockaddr saddr;

    sock->error = 0;
    sock->err = 0;

    rc = nl_address_getsockaddr(addr, &saddr);
    if (rc == -1) {
        sock->error = 1;
        return -1;
    }

    rc = connect(sock->fd, &saddr, sizeof(saddr));
    if (rc == 0) {
        nl_event_add_timer(&sock->wev, 0);
    }
    else if (errno != EINPROGRESS) {
        sock->error = 1;
        sock->err = errno;
        log_error("#%d connect: %s", sock->fd, strerror(sock->err));
        return -1;
    }
    else{
        nl_event_add(&sock->wev);
    }

    sock->chandler = sock->wev.handler;
    sock->wev.handler = nl_connect_wrapper;

    log_trace("#%d connecting", sock->fd);

    return 0;
}

int nl_recv(nl_socket_t *sock, char *buf, size_t len)
{
    int rc;

    sock->error = 0;
    sock->err = 0;

    rc = recv(sock->fd, buf, len, 0);
    if (rc < 0) {
        sock->err = errno;
        if (sock->err != EAGAIN && sock->err != EWOULDBLOCK) {
            sock->error = 1;
            log_error("#%d recv: %s", sock->fd, strerror(sock->err));
        }
        return -1;
    }

    log_trace("#%d recv %d bytes", sock->fd, rc);

    return rc;
}

int nl_recvfrom(nl_socket_t *sock, char *buf, size_t len, nl_address_t *addr)
{
    int rc, bytes;
    socklen_t slen;
    struct sockaddr saddr;
    const char *straddr;

    sock->error = 0;
    sock->err = 0;

    slen = sizeof(saddr);
    rc = recvfrom(sock->fd, buf, len, 0, &saddr, &slen);
    if (rc < 0) {
        sock->err = errno;
        if (sock->err != EAGAIN && sock->err != EWOULDBLOCK) {
            sock->error = 1;
            log_error("#%d recvfrom: %s", sock->fd, strerror(sock->err));
        }
        return -1;
    }
    bytes = rc;

    rc = nl_address_setsockaddr(addr, &saddr);
    if (rc == -1) {
        return -1;
    }

    straddr = nl_address_tostring(addr);
    if (straddr == NULL) {
        log_trace("#%d recvfrom(?:?) %d bytes", sock->fd, bytes);
    }
    else {
        log_trace("#%d recvfrom(%s) %d bytes", sock->fd, straddr, bytes);
    }

    return bytes;
}

int nl_send(nl_socket_t *sock, const char *buf, size_t len)
{
    int rc;

    sock->error = 0;
    sock->err = 0;

    rc = send(sock->fd, buf, len, MSG_NOSIGNAL);
    if (rc < 0) {
        sock->err = errno;
        if (sock->err != EAGAIN && sock->err != EWOULDBLOCK) {
            sock->error = 1;
            log_error("#%d send: %s", sock->fd,  strerror(sock->err));
        }
        return -1;
    }

    log_trace("#%d send %d bytes", sock->fd, rc);

    return rc;
}

int nl_sendto(nl_socket_t *sock, const char *buf, size_t len, nl_address_t *addr)
{
    int rc;
    struct sockaddr saddr;
    const char *straddr;

    sock->error = 0;
    sock->err = 0;

    rc = nl_address_getsockaddr(addr, &saddr);
    if (rc == -1) {
        sock->error = 1;
        return -1;
    }

    rc = sendto(sock->fd, buf, len, MSG_NOSIGNAL, &saddr, sizeof(saddr));
    if (rc < 0) {
        sock->err = errno;
        if (sock->err != EAGAIN && sock->err != EWOULDBLOCK) {
            sock->error = 1;
            straddr = nl_address_tostring(addr);
            if (straddr == NULL) {
                log_error("#%d sendto(?:?): %s", sock->fd, strerror(sock->err));
            }
            else {
                log_error("#%d sendto(%s): %s", sock->fd, straddr, strerror(sock->err));
            }
        }
        return -1;
    }

    straddr = nl_address_tostring(addr);
    if (straddr == NULL) {
        log_trace("#%d sendto(?:?) %d bytes", sock->fd, rc);
    }
    else {
        log_trace("#%d sendto(%s) %d bytes", sock->fd, straddr, rc);
    }

    return rc;
}

int nl_close(nl_socket_t *sock)
{
    if (sock->open == 0) {
        return 0;
    }

    nl_event_del(&sock->rev);
    nl_event_del(&sock->wev);

    sock->open = 0;

    log_trace("#%d closed", sock->fd);
    return close(sock->fd);
}

void nl_socket_copy(nl_socket_t *dst, nl_socket_t *src)
{
    memcpy(dst, src, sizeof(nl_socket_t));
    dst->wev.data = dst;
    dst->rev.data = dst;
}

int nl_socket_getsockname(nl_socket_t *sock, nl_address_t *addr)
{
    struct sockaddr sa;
    socklen_t sa_len;

    sa_len = sizeof(sa);
    if (getsockname(sock->fd, &sa, &sa_len) == -1) {
        log_error("#%d getsockname: %s", sock->fd, strerror(errno));
        return -1;
    }

    return nl_address_setsockaddr(addr, &sa);
}

int nl_socket_getpeername(nl_socket_t *sock, nl_address_t *addr)
{
    struct sockaddr sa;
    socklen_t sa_len;

    sa_len = sizeof(sa);
    if (getpeername(sock->fd, &sa, &sa_len) == -1) {
        log_error("#%d getsockname: %s", sock->fd, strerror(errno));
        return -1;
    }

    return nl_address_setsockaddr(addr, &sa);
}


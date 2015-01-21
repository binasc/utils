#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "socket.h"
#include "log.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

// TODO: handle signal

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

int nl_socket(nl_socket_t *sock)
{
    int fd, flags;

    sock->error = 0;
    sock->err = 0;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        sock->err = errno;
        sock->error = 1;
        log_error("socket: %s", strerror(sock->err));
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        sock->err = errno;
        sock->error = 1;
        log_error("fcntl F_GETFL: %s", strerror(sock->err));
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        sock->err = errno;
        sock->error = 1;
        log_error("fcntl F_SETFL: %s", strerror(sock->err));
        close(fd);
        return -1;
    }

    nl_socket_init(sock);
    sock->fd = fd;
    sock->open = 1;

    log_trace("#%d: created", fd);

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

    size = sizeof(struct sockaddr_in);
    fd = accept(sock->fd, (struct sockaddr *)&addr, &size);
    if (fd < 0) {
        err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            sock->error = 1;
            sock->err = err;
            log_error("accept: %s", strerror(sock->err));
        }
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        /* we do not set error flag */
        /* in order to distinguish  */
        /* the source of error      */
        nsock->error = 1;
        nsock->err = errno;
        log_error("fcntl F_GETFL: %s", strerror(nsock->err));
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        /* we do not set error flag */
        /* in order to distinguish  */
        /* the source of error      */
        nsock->error = 1;
        nsock->err = errno;
        log_error("fcntl F_SETFL: %s", strerror(nsock->err));
        close(fd);
        return -1;
    }

    nl_socket_init(nsock);
    nsock->fd = fd;
    nsock->open = 1;
    nsock->connected = 1;

    log_trace("#%d: accept #%d", sock->fd, fd);

    return 0;
}

int nl_listen(nl_socket_t *sock, struct sockaddr_in *addr, int backlog)
{
    int rc, on;

    sock->error = 0;
    sock->err = 0;
    on = 1;
    rc = setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR,
                    (const void *)&on , sizeof(int));
    if (rc == -1) {
        sock->error = 1;
        sock->err = errno;
        log_error("setsockopt: %s", strerror(sock->err));
        return -1;
    }

    rc = bind(sock->fd, (struct sockaddr *)addr, sizeof(*addr));
    if (rc == -1) {
        sock->error = 1;
        sock->err = errno;
        log_error("bind: %s", strerror(sock->err));
        return -1;
    }

    rc = listen(sock->fd, backlog);
    if (rc < 0) {
        sock->error = 1;
        sock->err = errno;
        log_error("listen: %s", strerror(sock->err));
        return -1;
    }

    sock->connected = 1;

    nl_event_add(&sock->rev);

    log_trace("#%d: listening", sock->fd);

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
        log_error("getsockopt: %s", strerror(sock->err));
        return -1;
    }
    else if (err != 0) {
        sock->error = 1;
        sock->err = err;
        log_error("connect: so_error: %d", err);
    }
    else {
        sock->connected = 1;
    }

    return 0;
}

static void nl_connect_wrapper(nl_event_t *ev)
{
    nl_socket_t    *sock;

    log_trace("nl_connect_wrapper");
    sock = ev->data;

    (void) nl_post_connect(sock);
    ev->handler = sock->chandler;

    ev->handler(ev);
}

int nl_connect(nl_socket_t *sock, struct sockaddr_in *addr)
{
    int             rc;

    sock->error = 0;
    sock->err = 0;
    rc = connect(sock->fd, (struct sockaddr *)addr, sizeof(*addr));
    if (rc == 0) {
        nl_event_add_timer(&sock->wev, 0);
    }
    else if (errno != EINPROGRESS) {
        sock->error = 1;
        sock->err = errno;
        log_error("connect: %s", strerror(sock->err));
        return -1;
    }
    else{
        nl_event_add(&sock->wev);
    }

    sock->chandler = sock->wev.handler;
    sock->wev.handler = nl_connect_wrapper;

    return 0;
}

int nl_recv(nl_socket_t *sock, char *buf, size_t len)
{
    int rc;

    sock->error = 0;
    sock->err = 0;
    rc = recv(sock->fd, buf, len, 0);
    if (rc < 0) {
        /* we do not set error flag */
        /* in order to distinguish  */
        /* whether an error really  */
        /* happened                 */
        sock->err = errno;
        if (sock->err != EAGAIN && sock->err != EWOULDBLOCK) {
            sock->error = 1;
            log_error("recv: %s", strerror(sock->err));
        }
        return -1;
    }

    log_trace("#%d: recv %d", sock->fd, rc);

    return rc;
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
            log_error("send: %s", strerror(sock->err));
        }
        return -1;
    }

    log_trace("#%d: send %d", sock->fd, rc);

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

    log_trace("#%d: closed", sock->fd);
    return close(sock->fd);
}


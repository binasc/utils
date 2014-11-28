#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include "network.h"

/* perror */
#include <stdio.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

int nl_select_init(select_t *ctx)
{
    memset(ctx, 0, sizeof(select_t));
    return 0;
}

int nl_select_loop(select_t *ctx)
{
    int i, rc, err;
    fd_set recv_fd_set, send_fd_set;
    select_socket_t *curr_sock;
    socklen_t len;
    struct timeval now, timeout;

    while (1) {
        recv_fd_set = ctx->recv_fd_set;
        send_fd_set = ctx->send_fd_set;

        gettimeofday(&now, NULL);
        timerclear(&timeout);
        if (ctx->num_events == 0) {
            timeout.tv_usec = 100 * 1000;
        }
        else if (!timercmp(&ctx->events[0]->end, &now, <)) {
            timersub(&ctx->events[0]->end, &now, &timeout);
        }

        rc = select(FD_SETSIZE, &recv_fd_set, &send_fd_set, NULL, &timeout);
        if (rc < 0) {
            perror("select");
        }

        for (i = 0; i < FD_SETSIZE; ++i) {
            curr_sock = &ctx->sockets[i];
            if (!curr_sock->open) {
                continue;
            }

            if (FD_ISSET(i, &recv_fd_set)) {
                if (curr_sock->type == NL_SOCKET_TYPE_SERVER) {
                    rc = curr_sock->ops.accept(ctx, curr_sock);
                }
                else {
                    rc = curr_sock->ops.receive(ctx, curr_sock);
                }
                if (rc == 0) {
                    FD_CLR(i, &ctx->recv_fd_set);
                }
            }

            if (!curr_sock->open) {
                continue;
            }

            if (FD_ISSET(i, &send_fd_set)) {
                if (!curr_sock->connected) {
                    len = sizeof(err);
                    rc = getsockopt(i, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (rc == -1) {
                        perror("getsockopt");
                    }
                    else if (err != 0) {
                        //log_error("connect: so_error: %d", err);
                        fprintf(stderr, "error %d\n", err);
                    }
                    else {
                        curr_sock->connected = 1;
                    }
                    rc = curr_sock->ops.connected(ctx, curr_sock);
                }
                else {
                    rc = curr_sock->ops.send(ctx, curr_sock);
                }
                if (rc == 0) {
                    FD_CLR(i, &ctx->send_fd_set);
                }
            }
        }

        gettimeofday(&now, NULL);
        for (i = 0; i < ctx->num_events; i++) {
            if (timercmp(&ctx->events[i]->end, &now, <)) {
                ctx->events[i]->handler(ctx->events[i]);
            }
            else {
                break;
            }
        }
        if (i) {
            memmove(&ctx->events[0], &ctx->events[i],
                    (ctx->num_events - i) * sizeof(select_event_t *));
            ctx->num_events -= i;
        }
    }

    return 0;
}

select_socket_t *nl_select_socket(select_t *ctx,
                                  select_op_t *ops, void *data)
{
    int fd, flags;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return NULL;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        close(fd);
        return NULL;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        perror("fcntl");
        close(fd);
        return NULL;
    }

    memset(&ctx->sockets[fd], 0, sizeof(select_socket_t));
    ctx->sockets[fd].fd = fd;
    ctx->sockets[fd].ops = *ops;
    ctx->sockets[fd].data = data;
    ctx->sockets[fd].ctx = ctx;
    ctx->sockets[fd].open = 1;

    return &ctx->sockets[fd];
}

int nl_select_listen(select_socket_t *sock,
                     unsigned short port, int backlog)
{
    int                 rc, on;
    struct sockaddr_in  addr;
    select_t            *ctx;

    on = 1;
    rc = setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR,
                    (const void *)&on , sizeof(int));
    if (rc == -1) {
        perror("setsockopt");
        close(sock->fd);
        return -1;
    }

    memset(&addr, sizeof(addr), 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("0.0.0.0");
    addr.sin_port = htons(port);

    rc = bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == -1) {
        perror("bind");
        close(sock->fd);
        return -1;
    }

    rc = listen(sock->fd, backlog);
    if (rc < 0) {
        perror("listen");
        close(sock->fd);
        return -1;
    }

    ctx = sock->ctx;
    ctx->sockets[sock->fd].type = NL_SOCKET_TYPE_SERVER;
    ctx->sockets[sock->fd].connected = 1;
    FD_SET(sock->fd, &ctx->recv_fd_set);

    return 0;
}

int nl_select_connect(select_socket_t *sock, struct sockaddr_in *addr)
{
    int         rc;
    select_t    *ctx;

    ctx = sock->ctx;

    rc = connect(sock->fd, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
    if (rc == 0) {
        sock->connected = 1;
        rc = ctx->sockets[sock->fd].ops.connected(ctx, sock);
        if (rc) {
            FD_SET(sock->fd, &ctx->send_fd_set);
        }
    }
    else if (errno != EINPROGRESS) {
        perror("connect");
        close(sock->fd);
        return -1;
    }
    else{
        FD_SET(sock->fd, &ctx->send_fd_set);
    }

    ctx->sockets[sock->fd].type = NL_SOCKET_TYPE_CLIENT;
    return 0;
}

select_socket_t *nl_select_accept(select_socket_t *sock,
                                  select_op_t *ops, void *data)
{
    int                 fd, flags;
    struct sockaddr_in  addr;
    socklen_t           size;
    select_t            *ctx;

    size = sizeof(struct sockaddr_in);
    fd = accept(sock->fd, (struct sockaddr *)&addr, &size);
    if (fd < 0) {
        perror("accept");
        close(sock->fd);
        return NULL;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        close(fd);
        return NULL;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        perror("fcntl");
        close(fd);
        return NULL;
    }

    ctx = sock->ctx;
    memset(&ctx->sockets[fd], 0, sizeof(select_socket_t));
    ctx->sockets[fd].fd = fd;
    ctx->sockets[fd].ops = *ops;
    ctx->sockets[fd].data = data;
    ctx->sockets[fd].type = NL_SOCKET_TYPE_CLIENT;
    ctx->sockets[fd].connected = 1;
    ctx->sockets[fd].ctx = ctx;
    ctx->sockets[fd].open = 1;

    return &ctx->sockets[fd];
}

int nl_select_recv(select_socket_t *sock, char *buf, size_t len)
{
    int rc, err;

    rc = recv(sock->fd, buf, len, 0);
    if (rc < 0) {
        err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return EAGAIN;
        }
        perror("recv");
    }

    return rc;
}

int nl_select_send(select_socket_t *sock, char *buf, size_t len)
{
    int rc, err;

    rc = send(sock->fd, buf, len, MSG_NOSIGNAL);
    if (rc < 0) {
        err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return EAGAIN;
        }
        perror("send");
    }

    return rc;
}

int nl_select_close(select_socket_t *sock)
{
    sock->open = 0;
    nl_select_stop_recv(sock);
    nl_select_stop_send(sock);
    return close(sock->fd);
}

void nl_select_begin_recv(select_socket_t *sock)
{
    FD_SET(sock->fd, &sock->ctx->recv_fd_set);
}

void nl_select_begin_send(select_socket_t *sock)
{
    FD_SET(sock->fd, &sock->ctx->send_fd_set);
}

void nl_select_stop_recv(select_socket_t *sock)
{
    FD_CLR(sock->fd, &sock->ctx->recv_fd_set);
}

void nl_select_stop_send(select_socket_t *sock)
{
    FD_CLR(sock->fd, &sock->ctx->send_fd_set);
}

int nl_queryname(const char *name, struct in_addr *addr)
{
    int rc;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    //hints.ai_family = AF_UNSPEC;  /* Allow IPv4 or IPv6 */
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    rc = getaddrinfo(name, NULL, &hints, &result);
    if (rc == -1) {
        //log_error("getaddrinfo: %s", gai_strerror(rc));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_addrlen == sizeof(struct sockaddr_in)) {
            *addr = ((struct sockaddr_in *)rp->ai_addr)->sin_addr;
            freeaddrinfo(result);
            return 0;
        }
    }

    freeaddrinfo(result);
    //log_error("cann't resolve %s", name);
    return -1;
}

static int event_less(const void *lhs, const void *rhs)
{
    const select_event_t *lev, *rev;
    lev = lhs;
    rev = rhs;

    if (timercmp(&lev->end, &lev->end, <)) {
        return -1;
    }
    else if (!timercmp(&lev->end, &rev->end, !=)) {
        return 0;
    }
    else {
        return 1;
    }
}

int nl_select_add_event(select_t *ctx, select_event_t *ev, int timer)
{
    if (ctx->num_events >= NL_SELECT_MAX_EVENT) {
        return 0;
    }

    ev->ctx = ctx;
    ev->timer_set = 1;
    gettimeofday(&ev->end, NULL);

    ev->end.tv_sec += timer / 1000;
    ev->end.tv_usec += (timer % 1000) * 1000;

    ctx->events[ctx->num_events++] = ev;

    qsort(ctx->events, ctx->num_events, sizeof(select_event_t *), event_less);
    return 0;
}

int nl_select_del_event(select_event_t *ev)
{
    int i;
    select_t *ctx;

    ctx = ev->ctx;
    ev->timer_set = 0;
    for (i = 0; i < ctx->num_events; i++) {
        if (ctx->events[i] == ev) {
            ctx->num_events--;
            memmove(&ctx->events[i], &ctx->events[i + 1],
                    (ctx->num_events - i) * sizeof(select_event_t *));
            return 0;
        }
    }

    return -1;
}


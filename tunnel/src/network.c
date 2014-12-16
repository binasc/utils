#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include "network.h"


#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

#define log_error(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#define log_debug(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#define log_trace(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)

// TODO: handle signal

int nl_select_init(nl_context_t *ctx)
{
    memset(ctx, 0, sizeof(nl_context_t));
    return 0;
}

static int event_less(const void *lhs, const void *rhs)
{
    const nl_event_t *lev, *rev;
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

int nl_select_loop(nl_context_t *ctx)
{
    int             i, j, rc, err;
    fd_set          recv_fd_set, send_fd_set;
    nl_socket_t     *curr_sock;
    socklen_t       len;
    struct timeval  now, timeout;
    size_t          old_num_events;

    while (1) {
        recv_fd_set = ctx->recv_fd_set;
        send_fd_set = ctx->send_fd_set;

        old_num_events = ctx->num_events;

        gettimeofday(&now, NULL);
        timerclear(&timeout);
        if (ctx->num_events == 0) {
            timeout.tv_usec = 500 * 1000;
        }
        else if (!timercmp(&ctx->events[0]->end, &now, <)) {
            timersub(&ctx->events[0]->end, &now, &timeout);
        }

        rc = select(FD_SETSIZE, &recv_fd_set, &send_fd_set, NULL, &timeout);
        if (rc < 0) {
            log_error("select: %s", strerror(errno));
        }

        for (i = 0; i < FD_SETSIZE; ++i) {
            curr_sock = &ctx->sockets[i];
            if (!curr_sock->open) {
                continue;
            }

            if (FD_ISSET(i, &recv_fd_set)) {
                if (curr_sock->type == NL_SOCKET_TYPE_SERVER) {
                    rc = curr_sock->ops.accept(curr_sock);
                }
                else {
                    rc = curr_sock->ops.receive(curr_sock);
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
                        log_error("getsockopt: %s", strerror(errno));
                    }
                    else if (err != 0) {
                        log_error("connect: so_error: %d", err);
                    }
                    else {
                        curr_sock->connected = 1;
                    }
                    rc = curr_sock->ops.connected(curr_sock);
                }
                else {
                    rc = curr_sock->ops.send(curr_sock);
                }
                if (rc == 0) {
                    FD_CLR(i, &ctx->send_fd_set);
                }
            }
        }

        //gettimeofday(&now, NULL);
        for (i = 0; i < old_num_events; i++) {
            if (ctx->events[i] == NULL) {
                continue;   /* already deleted */
            }
            if (!timercmp(&ctx->events[i]->end, &now, >)) {
                ctx->events[i]->handler(ctx->events[i]);
                ctx->events[i] = NULL;
            }
            else {
                break;
            }
        }

        for (j = 0; i < ctx->num_events; i++) {
            if (ctx->events[i] != NULL) {
                ctx->events[j++] = ctx->events[i];
                if (i > j) {
                    ctx->events[i] = NULL;
                }
            }
        }
        qsort(ctx->events, j, sizeof(nl_event_t *), event_less);
        ctx->num_events = j;
    }

    return 0;
}

nl_socket_t *nl_socket(nl_context_t *ctx)
{
    int fd, flags;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        log_error("socket: %s", strerror(errno));
        return NULL;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_error("fcntl F_GETFL: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        log_error("fcntl F_SETFL: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    memset(&ctx->sockets[fd], 0, sizeof(nl_socket_t));
    ctx->sockets[fd].ctx = ctx;
    ctx->sockets[fd].fd = fd;
    ctx->sockets[fd].open = 1;

    log_trace("#%d: created", fd);

    return &ctx->sockets[fd];
}

int nl_select_listen(nl_socket_t *sock, struct sockaddr_in *addr, int backlog)
{
    int                 rc, on;
    nl_context_t        *ctx;

    on = 1;
    rc = setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR,
                    (const void *)&on , sizeof(int));
    if (rc == -1) {
        log_error("setsockopt: %s", strerror(errno));
        close(sock->fd);
        return -1;
    }

    rc = bind(sock->fd, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
    if (rc == -1) {
        log_error("bind: %s", strerror(errno));
        close(sock->fd);
        return -1;
    }

    rc = listen(sock->fd, backlog);
    if (rc < 0) {
        log_error("listen: %s", strerror(errno));
        close(sock->fd);
        return -1;
    }

    ctx = sock->ctx;
    ctx->sockets[sock->fd].type = NL_SOCKET_TYPE_SERVER;
    ctx->sockets[sock->fd].connected = 1;
    FD_SET(sock->fd, &ctx->recv_fd_set);

    log_trace("#%d: listening", sock->fd);

    return 0;
}

int nl_select_connect(nl_socket_t *sock, struct sockaddr_in *addr)
{
    int             rc;
    nl_context_t    *ctx;

    ctx = sock->ctx;

    rc = connect(sock->fd, (struct sockaddr *)addr, sizeof(struct sockaddr_in));
    if (rc == 0) {
        sock->connected = 1;
        rc = ctx->sockets[sock->fd].ops.connected(sock);
        if (rc) {
            FD_SET(sock->fd, &ctx->send_fd_set);
        }
    }
    else if (errno != EINPROGRESS) {
        log_error("connect: %s", strerror(errno));
        close(sock->fd);
        return -1;
    }
    else{
        FD_SET(sock->fd, &ctx->send_fd_set);
    }

    ctx->sockets[sock->fd].type = NL_SOCKET_TYPE_CLIENT;
    return 0;
}

nl_socket_t *nl_select_accept(nl_socket_t *sock)
{
    int                 fd, err, flags;
    struct sockaddr_in  addr;
    socklen_t           size;
    nl_context_t        *ctx;

    sock->accept_error = 0;

    size = sizeof(struct sockaddr_in);
    fd = accept(sock->fd, (struct sockaddr *)&addr, &size);
    if (fd < 0) {
        err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            log_error("accept: %s", strerror(errno));
            sock->accept_error = 1;
        }
        return NULL;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_error("fcntl F_GETFL: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        log_error("fcntl F_SETFL: %s", strerror(errno));
        close(fd);
        return NULL;
    }

    ctx = sock->ctx;
    memset(&ctx->sockets[fd], 0, sizeof(nl_socket_t));
    ctx->sockets[fd].ctx = ctx;
    ctx->sockets[fd].fd = fd;
    ctx->sockets[fd].type = NL_SOCKET_TYPE_CLIENT;
    ctx->sockets[fd].connected = 1;
    ctx->sockets[fd].open = 1;

    log_trace("#%d: accept #%d", sock->fd, fd);

    return &ctx->sockets[fd];
}

int nl_select_recv(nl_socket_t *sock, char *buf, size_t len)
{
    int rc, err;

    rc = recv(sock->fd, buf, len, 0);
    if (rc < 0) {
        err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return -1;
        }
        log_error("recv: %s", strerror(errno));
        return -2;
    }

    log_trace("#%d: recv %d", sock->fd, rc);

    return rc;
}

int nl_select_send(nl_socket_t *sock, char *buf, size_t len)
{
    int rc, err;

    rc = send(sock->fd, buf, len, MSG_NOSIGNAL);
    if (rc < 0) {
        err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return -1;
        }
        log_error("send: %s", strerror(errno));
        return -2;
    }

    log_trace("#%d: send %d", sock->fd, rc);

    return rc;
}

int nl_select_close(nl_socket_t *sock)
{
    sock->open = 0;
    nl_select_stop_recv(sock);
    nl_select_stop_send(sock);

    log_trace("#%d: closed", sock->fd);

    return close(sock->fd);
}

void nl_select_start_recv(nl_socket_t *sock)
{
    FD_SET(sock->fd, &sock->ctx->recv_fd_set);
}

void nl_select_start_send(nl_socket_t *sock)
{
    FD_SET(sock->fd, &sock->ctx->send_fd_set);
}

void nl_select_stop_recv(nl_socket_t *sock)
{
    FD_CLR(sock->fd, &sock->ctx->recv_fd_set);
}

void nl_select_stop_send(nl_socket_t *sock)
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
        log_error("getaddrinfo: %s", gai_strerror(rc));
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
    log_error("cann't resolve %s", name);
    return -1;
}

int nl_select_add_event(nl_context_t *ctx, nl_event_t *ev, int timer)
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

    log_debug("trigger event after %d ms", timer);

    return 0;
}

int nl_select_del_event(nl_event_t *ev)
{
    int i;
    nl_context_t *ctx;

    ctx = ev->ctx;
    ev->timer_set = 0;
    for (i = 0; i < ctx->num_events; i++) {
        if (ctx->events[i] == ev) {
            ctx->events[i] = NULL;
            return 0;
        }
    }

    return -1;
}

/* wrapper */
static int wrapper_accept(nl_socket_t *sock);
static int wrapper_connected(nl_socket_t *sock);
static int wrapper_receive(nl_socket_t *sock);
static int wrapper_send(nl_socket_t *sock);

static nl_connection_t *nl_connection_create()
{
    nl_connection_t *c;

    c = calloc(1, sizeof(nl_connection_t));
    if (c == NULL) {
        return NULL;
    }

    c->tosend = list_create(sizeof(nl_buf_t), NULL, NULL);
    if (c->tosend == NULL) {
        free(c);
        return NULL;
    }

    return c;
}

static int wrapper_accept(nl_socket_t *sock)
{
    nl_socket_t         *nsock;
    nl_connection_t     *c, *nc;
    int                 rc;

    c = sock->data;
    c->error = 0;

    for ( ; ; ) {
        nsock = nl_select_accept(sock);
        if (nsock == NULL) {
            if (sock->accept_error) {
                c->error = 1;
                nl_connection_close(c);
                return 0;
            }
            return 1;
        }

        nc = nl_connection_create();
        if (nc == NULL) {
            nl_select_close(nsock);
            c->error = 1;
            nl_connection_close(c);
            return 0;
        }

        nsock->data = nc;
        nsock->ops.receive = wrapper_receive;
        nsock->ops.send = wrapper_send;
        nc->sock = nsock;

        rc = c->cbs.on_accepted(sock->data, nc);
    }

    return rc;
}

static int wrapper_connected(nl_socket_t *sock)
{
    nl_connection_t *c;

    c = sock->data;

    if (!sock->connected) {
        c->error = 1;
        nl_connection_close(c);
        return 0;
    }

    if (c->cbs.on_connected) {
        c->cbs.on_connected(c);
    }
    else {
        nl_select_start_recv(sock);
    }

    return list_empty(c->tosend) ? 0 : 1;
}

static int enlarge_buffer(nl_connection_t *c, size_t size)
{
    char *buf = NULL;

    if (size > c->remain_size) {
        buf = realloc(c->remain.buf, size);
        if (buf == NULL) {
            free(c->remain.buf);
            c->remain.buf = NULL;
            c->remain.len = 0;
            c->remain_size = 0;
            return -1;
        }
        else {
            c->remain.buf = buf;
            c->remain_size = size;
            return 0;
        }
    }

    return 0;
}

static int receive_handler(nl_connection_t *c, char *buf, size_t len)
{
    nl_buf_t    in, out;
    int         rc, n;

    if (c->remain.len == 0) {
        in.buf = buf;
        in.len = len;
    }
    else {
        if (enlarge_buffer(c, c->remain.len + len) < 0) {
            return -1;
        }
        memcpy(c->remain.buf + c->remain.len, buf, len);
        c->remain.len += len;
        in = c->remain;
    }

    rc = 1;
    if (c->cbs.splitter != NULL) {
        while ((n = c->cbs.splitter(c, &in, &out)) > 0) {
            in.buf += n;
            in.len -= n;
            if (out.len > 0) {
                rc = c->cbs.on_received(c, &out);
                if (rc == 0) {
                    break;
                }
            }
        }
    }
    else {
        out = in;
        in.buf += in.len;
        in.len = 0;
        rc = c->cbs.on_received(c, &out);
    }

    if (in.len > 0) {
        if (enlarge_buffer(c, in.len) < 0) {
            return -1;
        }
        memmove(c->remain.buf, in.buf, in.len);
    }
    c->remain.len = in.len;

    return rc;
}

static int wrapper_receive(nl_socket_t *sock)
{

// TODO: multi-threads
#define RECV_BUFF_SIZE 16384
    static char s_recv_buff[RECV_BUFF_SIZE];

    int rc;
    nl_connection_t *c;

    c = sock->data;
    c->error = 0;

    for ( ; ; ) {
        rc = nl_select_recv(sock, s_recv_buff, RECV_BUFF_SIZE);
        if (rc == 0) {
            break;
        }
        else if (rc > 0) {
            rc = receive_handler(sock->data, s_recv_buff, rc);
            if (rc < 0) {
                break;
            }
            else if (rc == 0) {
                return 0;
            }
        }
        else {
            if (rc == -1) {
                return 1;
            }
            else {
                c->error = 1;
                break;
            }
        }
    }

    nl_connection_close(c);
    return 0;
}

static void nl_connection_destroy(nl_connection_t *c)
{
    struct list_iterator_t  it;
    nl_buf_t                *buf;

    for (it = list_begin(c->tosend);
         !list_iterator_equal(list_end(c->tosend), it);
         it = list_iterator_next(it)) {
        buf = list_iterator_item(it);
        free(buf->buf);
    }
    list_destroy(c->tosend);
    if (c->remain.buf != NULL) {
        free(c->remain.buf);
        c->remain.buf = NULL;
        c->remain.len = 0;
        c->remain_size = 0;
    }

    log_debug("#%d: destroyed", c->sock->fd);

    nl_select_close(c->sock);

    if (c->cbs.on_closed != NULL) {
        c->cbs.on_closed(c);
    }

    free(c);
}

static void linger_handler(nl_event_t *ev)
{
    nl_connection_destroy((nl_connection_t *)ev->data);
}

static int wrapper_send(nl_socket_t *sock)
{
    nl_connection_t     *c;
    nl_buf_t            *buf, snd;
    int                 rc;

    c = sock->data;
    while (!list_empty(c->tosend)) {
        buf = (nl_buf_t *)list_front(c->tosend);
        rc = nl_select_send(sock, buf->buf, buf->len);
        snd.buf = buf->buf;
        snd.len = rc;
        if (rc == (int)buf->len) {
            if (c->cbs.on_sent) {
                c->cbs.on_sent(c, &snd);
            }
            free(buf->buf);
            list_pop_front(c->tosend);
        }
        else if (rc > 0 && rc < buf->len) {
            if (c->cbs.on_sent) {
                c->cbs.on_sent(c, &snd);
            }
            memmove(buf->buf, buf->buf + rc, buf->len - rc);
            buf->len -= rc;
        }
        else {
            if (rc == -1) {
                return 1;
            }
            else {
                c->error = 1;
                if (c->closing_ev.timer_set) {
                    nl_select_del_event(&c->closing_ev);
                }
                nl_connection_close(c);
                return 0;
            }
        }
    }

    rc = list_empty(c->tosend) ? 0 : 1;
    if (c->closing_ev.timer_set && rc == 0) {
        nl_select_del_event(&c->closing_ev);
        nl_connection_close(c);
    }

    return rc;
}

nl_connection_t *nl_connection(nl_context_t *ctx)
{
    nl_socket_t         *sock;
    nl_connection_t     *c;

    sock = nl_socket(ctx);
    if (sock == NULL) {
        return NULL;
    }

    c = nl_connection_create();
    if (c == NULL) {
        nl_select_close(sock);
        return NULL;
    }
    c->sock = sock;
    c->sock->ops.receive = wrapper_receive;
    c->sock->ops.send = wrapper_send;
    c->sock->data = c;

    return c;
}

int nl_connection_listen(nl_connection_t *c, struct sockaddr_in *addr, int backlog)
{
    c->sock->ops.accept = wrapper_accept;
    return nl_select_listen(c->sock, addr, backlog);
}

int nl_connection_connect(nl_connection_t *c, struct sockaddr_in *addr)
{
    c->sock->ops.connected = wrapper_connected;
    return nl_select_connect(c->sock, addr);
}

int nl_connection_send(nl_connection_t *c, nl_buf_t *buf)
{
    nl_buf_t tosend;

    if (c->closing_ev.timer_set) {
        return -1;
    }

    tosend.buf = malloc(buf->len);
    if (tosend.buf == NULL) {
        return -1;
    }
    memcpy(tosend.buf, buf->buf, buf->len);
    tosend.len = buf->len;
    list_push_back(c->tosend, &tosend);
    nl_select_start_send(c->sock);

    return 0;
}

size_t nl_connection_tosend_size(nl_connection_t *c)
{
    size_t s;
    struct list_iterator_t it;

    s = 0;
    for (it = list_begin(c->tosend);
         !list_iterator_equal(it, list_end(c->tosend));
         it = list_iterator_next(it)) {
        s += ((nl_buf_t *)list_iterator_item(it))->len;
    }

    return s;
}

int nl_connection_close(nl_connection_t *c)
{
    int timeout;

    if (c->closing_ev.timer_set) {
        return 0;
    }

    timeout = 0;
    if (c->error) {
        timeout = 0;
    }
    else if (!list_empty(c->tosend) /* && linger */) {
        timeout = 20000;
    }

    if (timeout == 0) {
        log_debug("#%d: closing in next loop", c->sock->fd);
    }
    else {
        log_debug("#%d: closing in %d ms", c->sock->fd, timeout);
    }

    c->closing_ev.handler = linger_handler;
    c->closing_ev.data = c;
    nl_select_add_event(c->sock->ctx, &c->closing_ev, timeout);
    return 0;
}

void nl_connection_pause_receiving(nl_connection_t *c)
{
    nl_select_stop_recv(c->sock);
}

void nl_connection_resume_receiving(nl_connection_t *c)
{
    nl_select_start_recv(c->sock);
    if (c->remain.len > 0) {
        wrapper_receive(c->sock);
    }
}

void nl_connection_pause_sending(nl_connection_t *c)
{
    nl_select_stop_send(c->sock);
}

void nl_connection_resume_sending(nl_connection_t *c)
{
    nl_select_start_send(c->sock);
}


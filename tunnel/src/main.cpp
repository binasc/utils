#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <string.h>
#include <map>
#include <list>
#include <string>
#include <cassert>

#include "utils.h"

#define log_error(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
//#define log_debug(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#define log_debug(fmt, ...)
//#define log_trace(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#define log_trace(fmt, ...)

using namespace std;

static int s_send_obs = 0;
static int s_recv_obs = 0;

void obscure_send_buff(char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        buf[i] = ~buf[i];
    }
}

void obscure_recv_buff(char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        buf[i] = ~buf[i];
    }
}

static fd_set active_rfd_set;
static fd_set active_sfd_set;

void notify_to_send(int sock)
{
    log_trace("%d beging sendng", sock);
    FD_SET(sock, &active_sfd_set);
}

void stop_sending(int sock)
{
    log_trace("%d stop sending", sock);
    FD_CLR(sock, &active_sfd_set);
}

class sock_ctx_t
{
public:
    ~sock_ctx_t();
    int (*read_handler)(int s, sock_ctx_t *ctx);
    int (*write_handler)(int s, sock_ctx_t *ctx);
    int connected: 1;
    int closed: 1;
    int peer_sock;
    time_t timeout;
    list<pair<char *, size_t> > send_list;
};

sock_ctx_t::~sock_ctx_t()
{
    list<pair<char *, size_t> >::iterator it;
    for (it = send_list.begin(); it != send_list.end(); it++) {
        free(it->first);
    }
    send_list.clear();
}

static map<int, sock_ctx_t *> s_socket_ctx;
static map<int, sock_ctx_t *> s_closed_ctx;

int peer_sock(int s)
{
    map<int, sock_ctx_t *>::iterator it;
    if ((it = s_socket_ctx.find(s)) != s_socket_ctx.end()) {
        return it->second->peer_sock;
    }

    return -1;
}

sock_ctx_t *peer_ctx(int s)
{
    map<int, sock_ctx_t *>::iterator it;
    if ((it = s_socket_ctx.find(s)) != s_socket_ctx.end()) {
        return s_socket_ctx[it->second->peer_sock];
    }

    return NULL;
}

int read_handler(int s, sock_ctx_t *ctx)
{
    int rc, err, nread;
    char *buf;
    sock_ctx_t *pctx;

    log_trace("%d read_handler", s);

    pctx = peer_ctx(s);
    if (pctx == NULL) {
        return -1;
    }

    nread = 0;
    while (1) {
        buf = (char *)malloc(4096);
        if (buf == NULL) {
            break;
        }

        rc = recv(s, buf, 4096, 0);
        if (rc == -1) {
            err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                break;
            }
            free(buf);
            perror("recv");
            return -1;
        }
        else if (rc == 0) {
            if (nread) {
                log_debug("%d read %d bytes", s, nread);
            }
            log_error("%d closed by peer", s);
            free(buf);
            return 0;
        }

        if (s_recv_obs) {
            obscure_recv_buff(buf, rc);
        }
        nread += rc;
        if (!pctx->closed) {
            log_trace("%d to send %d bytes", peer_sock(s), rc);
            log_trace("%d ctx: %p", peer_sock(s), pctx);
            if (s_send_obs) {
                obscure_send_buff(buf, rc);
            }
            pctx->send_list.push_back(make_pair(buf, (size_t)rc));
            notify_to_send(ctx->peer_sock);
        }
        else {
            free(buf);
        }
    }

    log_debug("%d read %d bytes", s, nread);
    return 1;
}

int write_handler(int s, sock_ctx_t *ctx)
{
    int rc, err, nsent;
    socklen_t len;

    log_trace("%d write_handler", s);

    if (!ctx->connected) {
        len = sizeof(err);
        rc = getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
        if (rc == -1) {
            perror("getsockopt");
            return -1;
        }
        if (err != 0) {
            log_error("%d connect: so_error: %d", s, err);
            return -1;
        }
        ctx->connected = 1;
        log_debug("%d connected", s);
    }

    nsent = 0;
    log_trace("%d ctx: %p", s, ctx);
    while (!ctx->send_list.empty()) {
        pair<char *, size_t> &p = ctx->send_list.front();
        rc = send(s, p.first, p.second, 0);
        if (rc == -1) {
            err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                break;
            }
            perror("send");
            return -1;
        }
        if (rc == (int)p.second) {
            free(p.first);
            ctx->send_list.pop_front();
        }
        else {
            memmove(p.first, p.first + rc, p.second - rc);
            p.second -= rc;
        }
        nsent += rc;
    }

    log_debug("%d sent %d bytes", s, nsent);
    return ctx->send_list.empty() ? 0 : 1;
}

int make_socket(uint16_t port)
{
    int rc, sock, flags, on;
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return -1;
    }

    flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        close(sock);
        return -1;
    }

    flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        perror("fcntl");
        close(sock);
        return -1;
    }

    if (port > 0) {
        on = 1;
        rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&on , sizeof(int));
        if (rc == -1) {
            perror("setsockopt");
            close(sock);
            return -1;
        }

        memset(&addr, sizeof(addr), 0);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("0.0.0.0");
        addr.sin_port = htons(port);

        rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
        if (rc == -1) {
            perror("bind");
            close(sock);
            return -1;
        }
    }

    return sock;
}

int accept_socket(int sock, struct sockaddr_in *addr)
{
    int nsock, flags;
    socklen_t size;

    size = sizeof(struct sockaddr_in);
    nsock = accept(sock, (struct sockaddr *)&addr, &size);
    if (nsock < 0) {
        perror("accept");
    }

    flags = fcntl(nsock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        close(nsock);
        return -1;
    }

    flags = fcntl(nsock, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
        perror("fcntl");
        close(nsock);
        return -1;
    }

    return nsock;
}

int connect_socket(int sock, struct sockaddr_in *addr)
{
    int rc;

    memset(addr, sizeof(struct sockaddr_in), 0);
    rc = connect(sock, (struct sockaddr *)addr, sizeof(struct sockaddr));
    if (rc == -1 && errno != EINPROGRESS) {
        perror("connect");
        close(sock);
        return -1;
    }

    return rc == -1 ? 0 : 1;
}

void wait_finishing_sending(int sock, sock_ctx_t *ctx, int timeout)
{
    ctx->closed = 1;
    ctx->timeout = time(NULL) + timeout;
    s_closed_ctx.insert(make_pair(sock, ctx));
}

void close_pair(int sock)
{
    int psock;
    sock_ctx_t *ctx, *pctx;
    map<int, sock_ctx_t *>::iterator it;

    if ((it = s_socket_ctx.find(sock)) != s_socket_ctx.end()) {
        ctx = it->second;
        psock = ctx->peer_sock;
        s_socket_ctx.erase(it);
    }
    else {
        log_error("sock: %d not found", sock);
        return;
    }

    if ((it = s_socket_ctx.find(psock)) != s_socket_ctx.end()) {
        pctx = it->second;
        s_socket_ctx.erase(it);
    }
    else {
        log_error("peer sock: %d not found", psock);
        return;
    }

    shutdown(sock, SHUT_RD);
    if (ctx->send_list.empty()) {
        delete ctx;
        FD_CLR(sock, &active_rfd_set);
        stop_sending(sock);
        close(sock);
        log_debug("%d closed", sock);
    }
    else {
        wait_finishing_sending(sock, ctx, 10);
    }

    shutdown(psock, SHUT_RD);
    if (pctx->send_list.empty()) {
        delete pctx;
        FD_CLR(psock, &active_rfd_set);
        stop_sending(psock);
        close(psock);
        log_debug("%d peer closed", psock);
    }
    else {
        wait_finishing_sending(psock, pctx, 10);
    }
}

static int s_daemon;
static int s_partner;

static string s_host;
static uint16_t s_rport;
static uint16_t s_lport;

int options(int argc, char ** argv)
{
    int iOpt = 0;
    int iQurVer = 0;

    while ((iOpt = getopt(argc, argv, "dPh:p:l:vSR")) != EOF)
    {
        switch (iOpt)
        {
            case 'd':
                s_daemon = 1;
                break;
            case 'P':
                s_partner = 1;
                break;
            case 'h':
                s_host = optarg;
                break;
            case 'p':
                s_rport = atoi(optarg);
                break;
            case 'l':
                s_lport = atoi(optarg);
                break;
            case 'v' :
                iQurVer = 1;
                break;
            case 'S':
                s_send_obs = 1;
                break;
            case 'R':
                s_recv_obs = 1;
                break;
            default :
                log_error("invalid option -- '%c'", iOpt);
                return -1;
        }
    }

    if (iQurVer)
    {
        printf("nlog server build in %s %s\n", __DATE__, __TIME__);
        return -1;
    }

    return 0;
}

int do_name_query(const char *name, struct sockaddr_in *addr)
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
            addr->sin_addr = ((struct sockaddr_in *)rp->ai_addr)->sin_addr;
            return 0;
        }
    }

    log_error("cann't resolve %s", name);
    return -1;
}

int main(int argc, char *argv[])
{
    int rc, i, sock, nsock, psock;
    fd_set read_fd_set, write_fd_set;
    struct sockaddr_in addr, remote_addr;;
    sock_ctx_t *ctx, *cli_ctx, *rmt_ctx;
    map<int, sock_ctx_t *>::iterator it;
    //struct timeval *tv;

    rc = options(argc, argv);
    if (rc == -1) {
        return -1;
    }

    if (s_daemon) {
        utils_T_daemon(".");
    }

    if (s_partner) {
        utils_T_partner("tunnel.pid", argv);
    }

    /* Create the socket and set it up to accept connections. */
    sock = make_socket(s_lport);
    if (listen(sock, 1) < 0) {
        perror ("listen");
        return -1;
    }

    memset(&remote_addr, sizeof(remote_addr), 0);
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(s_rport);
    rc = do_name_query(s_host.c_str(), &remote_addr);
    if (rc == -1) {
        return -1;
    }

    /* Initialize the set of active sockets. */
    FD_ZERO(&active_rfd_set);
    FD_ZERO(&active_sfd_set);
    FD_SET(sock, &active_rfd_set);

    while (1)
    {
        /* Block until input arrives on one or more active sockets. */
        read_fd_set = active_rfd_set;
        write_fd_set = active_sfd_set;
        log_trace("select");
        if (select(FD_SETSIZE, &read_fd_set, &write_fd_set, NULL, NULL) < 0) {
            perror("select");
        }

        /* Service all the sockets with input pending. */
        for (i = 0; i < FD_SETSIZE; ++i) {
            if (FD_ISSET (i, &read_fd_set)) {
                if (i == sock) {
                    nsock = accept_socket(sock, &addr);
                    if (nsock == -1) {
                        return -1;
                    }
                    psock = make_socket(0);
                    rc = connect_socket(psock, &remote_addr);
                    if (rc == -1) {
                        close(nsock);
                        continue;
                    }

                    cli_ctx = new sock_ctx_t();
                    cli_ctx->closed = 0;
                    cli_ctx->peer_sock = psock;
                    cli_ctx->read_handler = read_handler;
                    cli_ctx->write_handler = write_handler;
                    cli_ctx->connected = 1;

                    rmt_ctx = new sock_ctx_t();
                    rmt_ctx->closed = 0;
                    rmt_ctx->peer_sock = nsock;
                    rmt_ctx->read_handler = read_handler;
                    rmt_ctx->write_handler = write_handler;
                    rmt_ctx->connected  = rc;

                    s_socket_ctx.insert(make_pair(nsock, cli_ctx));
                    s_socket_ctx.insert(make_pair(psock, rmt_ctx));

                    log_debug("%d: accepted %d from host %s, port %d.",
                            i, nsock, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

                    FD_SET(nsock, &active_rfd_set);
                    FD_SET(psock, &active_rfd_set);
                    if (!rmt_ctx->connected) {
                        notify_to_send(psock);      // for connection
                    }
                }
                else {
                    ctx = NULL;
                    if ((it = s_socket_ctx.find(i)) != s_socket_ctx.end()) {
                        ctx = it->second;
                    }
                    if (ctx && ctx->read_handler(i, ctx) <= 0) {
                        close_pair(i);
                    }
                }
            }
            if (FD_ISSET(i, &write_fd_set)) {
                ctx = NULL;
                if ((it = s_socket_ctx.find(i)) != s_socket_ctx.end()) {
                    ctx = it->second;
                }
                else if ((it = s_closed_ctx.find(i)) != s_closed_ctx.end()) {
                    ctx = it->second;
                }

                if (ctx) {
                    rc = ctx->write_handler(i, ctx);
                    if (rc < 0) {
                        if (ctx->closed) {
                            stop_sending(i);
                            s_closed_ctx.erase(i);
                            delete ctx;
                            log_debug("%d closed", i);
                        }
                        else {
                            close_pair(i);
                        }
                    }
                    else if (rc == 0) {
                        stop_sending(i);
                        if (ctx->closed) {
                            s_closed_ctx.erase(i);
                            delete ctx;
                            log_debug("%d closed", i);
                        }
                    }
                    else {
                        notify_to_send(i);
                    }
                }
            }
        }

        // timeout
        time_t curr = time(NULL);
        map<int, sock_ctx_t *>::iterator it;
        for (it = s_closed_ctx.begin(); it != s_closed_ctx.end();) {
            if (it->second->timeout < curr) {
                FD_CLR(it->first, &active_rfd_set);
                stop_sending(it->first);
                close(it->first);
                delete it->second;
                s_closed_ctx.erase(it++);
                log_debug("%d timedout, closed", psock);

            }
            else {
                it++;
            }
        }
        log_trace("loop end");
    }
}


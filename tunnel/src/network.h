#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <sys/select.h>
#include <arpa/inet.h>
#include <sys/time.h>

struct select_s;
struct select_socket_s;

typedef struct select_op_s
{
    union {
        int (*accept)(struct select_s *, struct select_socket_s *);
        int (*connected)(struct select_s *s, struct select_socket_s *);
    };
    int (*receive)(struct select_s *, struct select_socket_s *);
    int (*send)(struct select_s *, struct select_socket_s *);
} select_op_t;

#define NL_SOCKET_TYPE_SERVER 0
#define NL_SOCKET_TYPE_CLIENT 1

typedef struct select_socket_s
{
    int                 fd;
    select_op_t         ops;
    struct sockaddr_in  addr;
    int                 type :1;
    int                 connected :1;
    int                 open :1;
    void                *data;
    struct select_s     *ctx;
} select_socket_t;

#define NL_SELECT_MAX_EVENT FD_SETSIZE
struct select_event_s;
typedef void (*handler_fn)(struct select_event_s *);
typedef struct select_event_s
{
    struct select_s     *ctx;
    struct timeval      end;
    handler_fn          handler;
    void                *data;
    int                 timer_set;
} select_event_t;

typedef struct select_s
{
    select_socket_t sockets[FD_SETSIZE];
    /* active fd set */
    fd_set recv_fd_set;
    fd_set send_fd_set;

    select_event_t *events[NL_SELECT_MAX_EVENT];
    int num_events;
} select_t;

int nl_select_init(select_t *ctx);
int nl_select_loop(select_t *ctx);
select_socket_t *nl_select_socket(select_t *ctx,
                                  select_op_t *op, void *data);
int nl_select_listen(select_socket_t *sock,
                     unsigned short port, int backlog);
int nl_select_connect(select_socket_t *sock, struct sockaddr_in *addr);
select_socket_t *nl_select_accept(select_socket_t *sock,
                                  select_op_t *op, void *data);
int nl_select_recv(select_socket_t *sock, char *buf, size_t len);
int nl_select_send(select_socket_t *sock, char *buf, size_t len);
int nl_select_close(select_socket_t *sock);

void nl_select_begin_recv(select_socket_t *sock);
void nl_select_begin_send(select_socket_t *sock);
void nl_select_stop_recv(select_socket_t *sock);
void nl_select_stop_send(select_socket_t *sock);

int nl_queryname(const char *name, struct in_addr *addr);

/* timer in ms */
int nl_select_add_event(select_t *ctx, select_event_t *ev, int timer);
int nl_select_del_event(select_event_t *ev);

#endif


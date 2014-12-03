#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <sys/select.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "list.h"

struct nl_context_s;
struct nl_socket_s;

typedef struct nl_socket_ops_s
{
    union {
        int (*accept)(struct nl_socket_s *);
        int (*connected)(struct nl_socket_s *);
    };
    int (*receive)(struct nl_socket_s *);
    int (*send)(struct nl_socket_s *);
} nl_socket_ops_t;

#define NL_SOCKET_TYPE_SERVER 0
#define NL_SOCKET_TYPE_CLIENT 1

typedef struct nl_socket_s
{
    struct nl_context_s *ctx;
    struct sockaddr_in  addr;
    int                 fd;
    unsigned            type :1;
    unsigned            connected :1;
    unsigned            open :1;
    unsigned            accept_error :1;
    nl_socket_ops_t     ops;
    void                *data;
} nl_socket_t;

#define NL_SELECT_MAX_EVENT FD_SETSIZE
struct nl_event_s;
typedef void (*handler_fn)(struct nl_event_s *);

typedef struct nl_event_s
{
    struct nl_context_s *ctx;
    struct timeval      end;
    handler_fn          handler;
    void                *data;
    int                 timer_set :1;
} nl_event_t;

typedef struct nl_context_s
{
    nl_socket_t         sockets[FD_SETSIZE];
    /* active fd set */
    fd_set              recv_fd_set;
    fd_set              send_fd_set;

    size_t              num_events;
    nl_event_t          *events[NL_SELECT_MAX_EVENT];
} nl_context_t;

int nl_select_init(nl_context_t *ctx);
int nl_select_loop(nl_context_t *ctx);

nl_socket_t *nl_socket(nl_context_t *ctx);
int nl_select_listen(nl_socket_t *sock,
                     unsigned short port, int backlog);
int nl_select_connect(nl_socket_t *sock, struct sockaddr_in *addr);

nl_socket_t *nl_select_accept(nl_socket_t *sock);
int nl_select_recv(nl_socket_t *sock, char *buf, size_t len);
int nl_select_send(nl_socket_t *sock, char *buf, size_t len);
int nl_select_close(nl_socket_t *sock);

void nl_select_begin_recv(nl_socket_t *sock);
void nl_select_begin_send(nl_socket_t *sock);
void nl_select_stop_recv(nl_socket_t *sock);
void nl_select_stop_send(nl_socket_t *sock);

int nl_queryname(const char *name, struct in_addr *addr);

/* timer in ms */
int nl_select_add_event(nl_context_t *ctx, nl_event_t *ev, int timer);
int nl_select_del_event(nl_event_t *ev);

/* wrapper */

typedef struct nl_buf_s
{
    char    *buf;
    size_t  len;
} nl_buf_t;

struct nl_connection_s;

typedef struct nl_callback_s 
{
    union {
        int (*on_accepted)(struct nl_connection_s *, struct nl_connection_s *);
        int (*on_connected)(struct nl_connection_s *);
    };
    /* optional */
    int (*splitter)(const nl_buf_t *in, nl_buf_t *out);
    int (*on_received)(struct nl_connection_s *, nl_buf_t *buf);
    /* optional */
    int (*on_sent)(struct nl_connection_s *, nl_buf_t *buf);
    /* optional */
    int (*on_closed)(struct nl_connection_s *);
} nl_callback_t;

typedef struct nl_connection_s
{
    nl_socket_t         *sock;
    nl_callback_t       cbs;
    void                *data;
    nl_buf_t            remain;
    size_t              remain_size;
    struct list_t       *tosend;
    unsigned            error :1;
    nl_event_t          closing_ev;
} nl_connection_t;

nl_connection_t *nl_connection(nl_context_t *ctx);
int nl_connection_listen(nl_connection_t *c,
                         unsigned short port, int backlog);
int nl_connection_connect(nl_connection_t *c, struct sockaddr_in *addr);
int nl_connection_send(nl_connection_t *c, nl_buf_t *buf);
int nl_connection_close(nl_connection_t *c);

#endif


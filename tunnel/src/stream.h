#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "socket.h"
#include "event.h"
#include "list.h"
#include "buffer.h"

struct nl_connection_s;

typedef struct nl_callback_s 
{
    void (*on_accepted)(struct nl_connection_s *, struct nl_connection_s *);
    void (*on_connected)(struct nl_connection_s *);
    /* optional */
    int (*splitter)(struct nl_connection_s *c, const nl_buf_t *in, nl_buf_t *out);
    void (*on_received)(struct nl_connection_s *, nl_buf_t *buf);
    /* optional */
    void (*on_sent)(struct nl_connection_s *, nl_buf_t *buf);
    /* optional */
    void (*on_closed)(struct nl_connection_s *);
} nl_callback_t;

typedef struct nl_connection_s
{
    nl_socket_t         sock;
    nl_callback_t       cbs;
    void                *data;
    nl_buf_t            remain;
    size_t              remain_size;
    struct list_t       *tosend;
    unsigned            error :1;
    nl_event_t          closing_ev;
} nl_connection_t;

nl_connection_t *nl_connection();
int nl_connection_listen(nl_connection_t *c, nl_address_t *addr, int backlog);
int nl_connection_connect(nl_connection_t *c, nl_address_t *addr);
int nl_connection_send(nl_connection_t *c, nl_buf_t *buf);
int nl_connection_close(nl_connection_t *c);

void nl_connection_pause_receiving(nl_connection_t *c);
void nl_connection_resume_receiving(nl_connection_t *c);
void nl_connection_pause_sending(nl_connection_t *c);
void nl_connection_resume_sending(nl_connection_t *c);

#endif


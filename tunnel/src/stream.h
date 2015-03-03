#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "socket.h"
#include "event.h"
#include "list.h"
#include "buffer.h"

struct nl_stream_s;

typedef struct nl_callback_s 
{
    void (*on_accepted)(struct nl_stream_s *);
    void (*on_connected)(struct nl_stream_s *);
    /* optional */
    int (*splitter)(struct nl_stream_s *c, const nl_buf_t *in, nl_buf_t *out);
    void (*on_received)(struct nl_stream_s *, nl_buf_t *buf);
    /* optional */
    void (*on_sent)(struct nl_stream_s *, nl_buf_t *buf);
    /* optional */
    void (*on_closed)(struct nl_stream_s *);
} nl_callback_t;

typedef struct nl_stream_s
{
    nl_socket_t         sock;
    nl_callback_t       cbs;
    void                *data;

    nl_socket_t         accepted;

    nl_buf_t            remain;
    size_t              remain_size;
    struct list_t       *tosend;
    unsigned            error :1;
    nl_event_t          closing_ev;
} nl_stream_t;

int nl_stream(nl_stream_t *s);
int nl_stream_listen(nl_stream_t *c, nl_address_t *addr, int backlog);
int nl_stream_accept(nl_stream_t *acceptor, nl_stream_t *s);
int nl_stream_connect(nl_stream_t *c, nl_address_t *addr);
int nl_stream_send(nl_stream_t *c, nl_buf_t *buf);
int nl_stream_close(nl_stream_t *c);

void nl_stream_pause_receiving(nl_stream_t *c);
void nl_stream_resume_receiving(nl_stream_t *c);
void nl_stream_pause_sending(nl_stream_t *c);
void nl_stream_resume_sending(nl_stream_t *c);

#endif


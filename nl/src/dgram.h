#ifndef __DGRAM_H__
#define __DGRAM_H__

#include "socket.h"
#include "event.h"
#include "list.h"
#include "buffer.h"

typedef struct nl_dgram_s
{
    nl_socket_t         sock;
    void               *data;

    void              (*on_received)(struct nl_dgram_s *, nl_packet_t *);
    void              (*on_sent)(struct nl_dgram_s *, nl_packet_t *);
    void              (*on_closed)(struct nl_dgram_s *);

    struct list_t      *tosend;

    unsigned            error :1;
    nl_event_t          closing_ev;
} nl_dgram_t;

int nl_dgram(nl_dgram_t *d);
int nl_dgram_bind(nl_dgram_t *d, nl_address_t *addr);
int nl_dgram_send(nl_dgram_t *d, nl_packet_t *p);
int nl_dgram_close(nl_dgram_t *d);

void nl_dgram_resume_receiving(nl_dgram_t *d);
void nl_dgram_pause_receiving(nl_dgram_t *d);

#endif


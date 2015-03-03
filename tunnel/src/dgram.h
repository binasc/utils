#ifndef __DGRAM_H__
#define __DGRAM_H__

#include "socket.h"
#include "event.h"
#include "list.h"
#include "buffer.h"

typedef struct nl_datagram_s
{
    nl_socket_t         sock;
    void               *data;

    void              (*on_received)(struct nl_datagram_s *, nl_packet_t *);
    void              (*on_sent)(struct nl_datagram_s *, nl_packet_t *);
    void              (*on_closed)(struct nl_datagram_s *);

    struct list_t      *tosend;

    unsigned            error :1;
    nl_event_t          closing_ev;
} nl_datagram_t;

int nl_datagram(nl_datagram_t *d);
int nl_datagram_bind(nl_datagram_t *d, nl_address_t *addr);
int nl_datagram_send(nl_datagram_t *d, nl_packet_t *p);
int nl_datagram_close(nl_datagram_t *d);

#endif


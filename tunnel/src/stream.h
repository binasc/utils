#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "socket.h"
#include "event.h"
#include "list.h"
#include "buffer.h"

struct nl_stream_s;
struct nl_decoder_s;
struct nl_encoder_s;

typedef int (*nl_decoder_fn)(void *, const nl_buf_t *, nl_buf_t *);
typedef char *(*nl_encoder_fn)(void *, char *, size_t *);

typedef struct nl_decoder_s {
    nl_decoder_fn           decoder;
    struct nl_decoder_s     *next;

    nl_buf_t                remain;
    size_t                  remain_size;

    void                    *data;
} nl_decoder_t;

typedef struct nl_encoder_s {
    nl_encoder_fn           encoder;
    struct nl_encoder_s     *next;

    void                    *data;
} nl_encoder_t;

typedef struct nl_callback_s 
{
    void (*on_accepted)(struct nl_stream_s *);
    void (*on_connected)(struct nl_stream_s *);

    void (*on_received)(struct nl_stream_s *, nl_buf_t *buf);
    /* optional */
    void (*on_sent)(struct nl_stream_s *, nl_buf_t *buf);
    /* optional */
    void (*on_closed)(struct nl_stream_s *);
} nl_callback_t;

// TODO: seperate acceptor
typedef struct nl_stream_s
{
    nl_socket_t         sock;
    nl_callback_t       cbs;

    /* optional */
    nl_decoder_t        *decoders;
    nl_encoder_t        *encoders;

    void                *data;

    nl_socket_t         accepted_sock;

    struct list_t       *tosend;

    unsigned            id;     /* id == fd */
    unsigned            error :1;
    unsigned            closed :1;
    unsigned            accepted :1;
    nl_event_t          closing_ev;
} nl_stream_t;

int nl_stream(nl_stream_t *s);
int nl_stream_listen(nl_stream_t *s, nl_address_t *addr, int backlog);
int nl_stream_accept(nl_stream_t *acceptor, nl_stream_t *s);
int nl_stream_connect(nl_stream_t *s, nl_address_t *addr);
int nl_stream_send(nl_stream_t *s, nl_buf_t *buf);
int nl_stream_close(nl_stream_t *s);

int nl_stream_encoder_push_back(nl_stream_t *s, nl_encoder_fn enc, void *data);
int nl_stream_encoder_pop_back(nl_stream_t *s);

int nl_stream_decoder_push_back(nl_stream_t *s, nl_decoder_fn dec, void *data);
int nl_stream_decoder_pop_back(nl_stream_t *s);

size_t nl_stream_pending_bytes(nl_stream_t *s);
void nl_stream_pause_receiving(nl_stream_t *s);
void nl_stream_resume_receiving(nl_stream_t *s);
void nl_stream_pause_sending(nl_stream_t *s);
void nl_stream_resume_sending(nl_stream_t *s);

int nl_stream_getsockname(nl_stream_t *s, nl_address_t *addr);
int nl_stream_getpeername(nl_stream_t *s, nl_address_t *addr);

#endif


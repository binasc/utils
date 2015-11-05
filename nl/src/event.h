#ifndef __EVENT_H__
#define __EVENT_H__

#include <sys/time.h>

struct nl_event_s;
typedef void (*handler_fn)(struct nl_event_s *);

#define NL_INVALID_INDEX  0xd0d0d0d0

typedef struct nl_event_s
{
    int                 fd;
    unsigned            index;
    struct timeval      timeout;

    handler_fn          handler;
    void                *data;

    unsigned            active :1;
    unsigned            write :1;
    unsigned            timer_set :1;

    struct nl_event_s  *next;
} nl_event_t;

typedef unsigned int nl_msec_t;
#define NL_MAX_TIMERS 1024
#define NL_TIMER_INFINITE ((nl_msec_t)-1)

void nl_post_event(nl_event_t *ev);
void nl_process_loop(void);

int nl_event_add_timer(nl_event_t *ev, nl_msec_t timer);
int nl_event_del_timer(nl_event_t *ev);

typedef struct nl_event_actions_s
{
    int (*init)(void);
    int (*done)(void);

    int (*add)(nl_event_t *);
    int (*del)(nl_event_t *);

    int (*process_events)(nl_msec_t timer);
} nl_event_actions_t;

extern nl_event_actions_t nl_event_actions;

#define nl_event_init nl_event_actions.init
#define nl_event_add nl_event_actions.add
#define nl_event_del nl_event_actions.del

#endif


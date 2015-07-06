#include <stdlib.h>
#include "event.h"
#include "log.h"

static size_t               loop_ntimers;
static size_t               ntimers;
static nl_event_t          *timer_index[NL_MAX_TIMERS];
static nl_event_t          *posted_events;

void nl_post_event(nl_event_t *ev)
{
    if (posted_events == NULL) {
        posted_events = ev;
        ev->next = NULL;
    }
    else {
        ev->next = posted_events;
        posted_events = ev;
    }
}

static int event_less(const void *lhs, const void *rhs)
{
    const nl_event_t *lev, *rev;
    lev = lhs;
    rev = rhs;

    if (timercmp(&lev->timeout, &lev->timeout, <)) {
        return -1;
    }
    else if (!timercmp(&lev->timeout, &rev->timeout, !=)) {
        return 0;
    }
    else {
        return 1;
    }
}

static nl_msec_t nl_timeval2msec(struct timeval *tv)
{
    return tv->tv_sec * 1000 + tv->tv_usec / 1000;
}

static nl_msec_t nl_event_find_timer(void)
{
    struct timeval  now, res;

    if (ntimers == 0) {
        return NL_TIMER_INFINITE;
    }

    gettimeofday(&now, NULL);
    if (timercmp(&timer_index[0]->timeout, &now, >)) {
        timersub(&timer_index[0]->timeout, &now, &res);
        return nl_timeval2msec(&res);
    }

    return 0;
}

static void nl_event_expire_timers(void)
{
    int             i, j;
    struct timeval  now;
    nl_event_t      *ev;

    gettimeofday(&now, NULL);
    for (i = 0; i < loop_ntimers; i++) {
        if (timer_index[i] == NULL) {
            continue;   /* already deleted */
        }

        if (!timercmp(&timer_index[i]->timeout, &now, >)) {
            ev = timer_index[i];
            nl_event_del_timer(ev);

            ev->handler(ev);
        }
        else {
            break;
        }
    }

    for (j = 0; i < ntimers; i++) {
        if (timer_index[i] != NULL) {
            timer_index[j] = timer_index[i];
            if (i > j) {
                timer_index[i] = NULL;
            }
            j++;
        }
    }

    ntimers = j;
    qsort(timer_index, ntimers, sizeof(nl_event_t *), event_less);
}

static void nl_process_events_and_timers(void)
{
    nl_msec_t       timer;
    nl_event_t     *ev;

    log_trace("#ev nl_process_events_and_timers");

    timer = nl_event_find_timer();

    loop_ntimers = ntimers;
    posted_events = NULL;

    (void) nl_event_actions.process_events(timer);

    for (ev = posted_events; ev != NULL; ev = ev->next) {
        ev->handler(ev);
    }

    nl_event_expire_timers();
}

void nl_process_loop(void)
{
    for ( ; ; ) {
        nl_process_events_and_timers();
    }
}

int nl_event_add_timer(nl_event_t *ev, nl_msec_t timer)
{
    if (ev->timer_set) {
        log_warning("#ev event timer is already set");
        return 0;
    }

    if (ntimers == NL_MAX_TIMERS) {
        log_error("#ev event timer list is full");
        return -1;
    }

    gettimeofday(&ev->timeout, NULL);

    ev->timeout.tv_sec += timer / 1000;
    ev->timeout.tv_usec += (timer % 1000) * 1000;

    ev->timer_set = 1;
    timer_index[ntimers++] = ev;

    log_debug("#ev trigger timer after %d ms", timer);

    return 0;
}

int nl_event_del_timer(nl_event_t *ev)
{
    int i;

    if (!ev->timer_set) {
        return 0;
    }

    for (i = 0; i < ntimers; i++) {
        if (timer_index[i] == ev) {
            timer_index[i] = NULL;
            ev->timer_set = 0;
            // intended
            //ntimers--;
            return 0;
        }
    }

    log_error("#ev event timer can not be found");
    return -1;
}


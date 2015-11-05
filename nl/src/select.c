#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "event.h"
#include "log.h"

static int nl_select_init(void);
static int nl_select_done(void);
static int nl_select_add_event(nl_event_t *ev);
static int nl_select_del_event(nl_event_t *ev);
static int nl_select_process_events(nl_msec_t timer);

nl_event_actions_t nl_event_actions = 
{
    nl_select_init,
    nl_select_done,
    nl_select_add_event,
    nl_select_del_event,
    nl_select_process_events,
};

static fd_set           master_read_fd_set;
static fd_set           master_write_fd_set;

static nl_event_t     **event_index;
static ssize_t          max_fd;
static size_t           nevents;

static int nl_select_init(void)
{
    event_index = malloc(FD_SETSIZE * 2);
    if (event_index == NULL) {
        return -1;
    }

    nevents = 0;

    max_fd = -1;

    return 0;
}

static int nl_select_done(void)
{
    free(event_index);

    event_index = NULL;

    return 0;
}

static int nl_select_add_event(nl_event_t *ev)
{
    if (ev->index != NL_INVALID_INDEX) {
        log_warning("select event fd: %d is already set %s",
                ev->fd, ev->write ? "write" : "read");
        return 0;
    }

    if (ev->write) {
        FD_SET(ev->fd, &master_write_fd_set);
    }
    else {
        FD_SET(ev->fd, &master_read_fd_set);
    }

    if (max_fd != -1 && ev->fd > max_fd) {
        max_fd = ev->fd;
    }

    ev->active = 1;

    event_index[nevents] = ev;
    ev->index = nevents;
    nevents++;

    return 0;
}

static int nl_select_del_event(nl_event_t *ev)
{
    if (ev->index == NL_INVALID_INDEX) {
        return 0;
    }

    ev->active = 0;

    if (ev->write) {
        FD_CLR(ev->fd, &master_write_fd_set);
    }
    else {
        FD_CLR(ev->fd, &master_read_fd_set);
    }

    if (ev->fd == max_fd) {
        max_fd = -1;
    }

    if (ev->index < --nevents) {
        event_index[ev->index] = event_index[nevents];
        event_index[ev->index]->index = ev->index;
    }

    ev->index = NL_INVALID_INDEX;

    return 0;
}

static int nl_select_process_events(nl_msec_t timer)
{
    int             i, ready, err, found, nready;
    fd_set          read_fd_set, write_fd_set;
    nl_event_t      *ev;
    struct timeval  tv, *tp;

    read_fd_set = master_read_fd_set;
    write_fd_set = master_write_fd_set;

    if (timer == NL_TIMER_INFINITE) {
        tp = NULL;
    }
    else {
        tv.tv_sec = (long) timer / 1000;
        tv.tv_usec = (long) (timer % 1000) * 1000;
        tp = &tv;
    }

    if (max_fd == -1) {
        for (i = 0; i < nevents; i++) {
            ev = event_index[i];
            if (max_fd < ev->fd) {
                max_fd = ev->fd;
            }
        }
    }

    ready = select(max_fd + 1, &read_fd_set, &write_fd_set, NULL, tp);
    if (ready < 0) {
        err = errno;
        if (err == EINTR) {
            return 0;
        }
        
        log_error("select: %s", strerror(err));
        return -1;
    }

    if (ready == 0) {
        if (timer != NL_TIMER_INFINITE) {
            return 0;
        }

        log_error("select() returned no events without timeout");
        return -1;
    }

    nready = 0;

    for (i = 0; i < nevents; i++) {
        ev = event_index[i];
        found = 0;

        if (ev->write) {
            if (FD_ISSET(ev->fd, &write_fd_set)) {
                found = 1;
            }
        }
        else {
            if (FD_ISSET(ev->fd, &read_fd_set)) {
                found = 1;
            }
        }

        if (found) {
            nl_post_event(ev);
            nready++;
        }
    }

    if (nready != ready) {
        log_error("select ready != events: %d:%d", ready, nready);
    }

    return 0;
}


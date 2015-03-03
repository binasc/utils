#include "dgram.h"
#include "log.h"
#include <string.h>

static void nl_dgram_destroy(nl_dgram_t *c)
{
    struct list_iterator_t  it;
    nl_packet_t             *p;

    for (it = list_begin(c->tosend);
         !list_iterator_equal(list_end(c->tosend), it);
         it = list_iterator_next(it)) {
        p = list_iterator_item(it);
        free(p->buf.buf);
    }
    list_destroy(c->tosend);

    log_debug("#%d destroyed", c->sock.fd);

    nl_close(&c->sock);

    if (c->on_closed) {
        c->on_closed(c);
    }
}

static void udp_linger_handler(nl_event_t *ev)
{
    log_trace("udp_linger_handler");
    nl_dgram_destroy((nl_dgram_t *)ev->data);
}

static void udp_read_handler(nl_event_t *ev)
{
// TODO: multi-threads
#define RECV_BUFF_SIZE 16384
    static char s_recv_buff[RECV_BUFF_SIZE];

    int             rc;
    nl_socket_t    *sock;
    nl_dgram_t  *d;
    nl_packet_t     p;
    nl_address_t    addr;

    sock = ev->data;
    log_trace("#%d udp_read_handler", sock->fd);
    d = sock->data;
    d->error = 0;

    for ( ; ; ) {
        rc = nl_address_setsockaddr(&addr, &p.addr);
        if (rc == -1) {
            d->error = 1;
            break;
        }

        rc = nl_recvfrom(sock, s_recv_buff, RECV_BUFF_SIZE, &addr);
        if (rc < 0) {
            if (!sock->error) {
                /* EAGAIN || EWOULDBLOCK */
                return;
            }
            d->error = 1;
            break;
        }
        else {
            p.buf.buf = s_recv_buff;
            p.buf.len = rc;
            d->on_received(d, &p);
            if (!sock->rev.active) {
                return;
            }
        }
    }

    nl_dgram_close(d);
}

static void udp_write_handler(nl_event_t *ev)
{
    int                 rc;
    nl_socket_t         *sock;
    nl_dgram_t       *d;
    nl_packet_t         *p;
    nl_buf_t            *buf;
    nl_address_t        addr;

    sock = ev->data;
    log_trace("#%d udpwrite_handler", sock->fd);
    d = sock->data;
    while (!list_empty(d->tosend)) {
        p = (nl_packet_t *)list_front(d->tosend);
        buf = &p->buf;

        rc = nl_address_setsockaddr(&addr, &p->addr);
        if (rc == -1) {
            d->error = 1;
            if (d->closing_ev.timer_set) {
                nl_event_del_timer(&d->closing_ev);
            }
            nl_dgram_close(d);
            return;
        }

        rc = nl_sendto(sock, buf->buf, buf->len, &addr);
        if (rc == (int)buf->len) {
            if (d->on_sent) {
                d->on_sent(d, p);
            }
            free(buf->buf);
            list_pop_front(d->tosend);
        }
        else if (rc > 0 && rc < buf->len) {
            log_fatal("?????????????????????");
            if (d->on_sent) {
                d->on_sent(d, p);
            }
            memmove(buf->buf, buf->buf + rc, buf->len - rc);
            buf->len -= rc;
        }
        else {
            if (rc == -1 && !sock->error) {
                //nl_event_add(&sock->wev);
            }
            else {
                d->error = 1;
                if (d->closing_ev.timer_set) {
                    nl_event_del_timer(&d->closing_ev);
                }
                nl_dgram_close(d);
                return;
            }
        }
    }

    if (list_empty(d->tosend)) {
        nl_event_del(&d->sock.wev);
        if (d->closing_ev.timer_set) {
            nl_event_del_timer(&d->closing_ev);
            nl_dgram_close(d);
        }
    }
}

int nl_dgram(nl_dgram_t *d)
{
    int rc;

    memset(d, 0, sizeof(nl_dgram_t));

    d->tosend = list_create(sizeof(nl_packet_t), NULL, NULL);
    if (d->tosend == NULL) {
        return -1;
    }

    rc = nl_socket(&d->sock, NL_UDP);
    if (rc == -1) {
        d->error = 1;
        nl_dgram_close(d);
        return -1;
    }

    d->sock.data = d;
    d->sock.rev.handler = udp_read_handler;
    d->sock.wev.handler = udp_write_handler;

    return 0;
}

int nl_dgram_bind(nl_dgram_t *d, nl_address_t *addr)
{
    return nl_bind(&d->sock, addr);
}

int nl_dgram_send(nl_dgram_t *d, nl_packet_t *p)
{
    nl_packet_t tosend;

    if (d->closing_ev.timer_set) {
        return -1;
    }

    tosend.buf.buf = malloc(p->buf.len);
    if (tosend.buf.buf == NULL) {
        return -1;
    }
    memcpy(&tosend.addr, &p->addr, sizeof(tosend.addr));
    memcpy(tosend.buf.buf, p->buf.buf, p->buf.len);
    tosend.buf.len = p->buf.len;

    if (list_empty(d->tosend)) {
        nl_event_add(&d->sock.wev);
    }

    list_push_back(d->tosend, &tosend);

    return 0;
}

int nl_dgram_close(nl_dgram_t *d)
{
    if (d->closing_ev.timer_set) {
        return 0;
    }

    nl_event_del(&d->sock.wev);
    nl_event_del(&d->sock.rev);
    d->closing_ev.handler = udp_linger_handler;
    d->closing_ev.data = d;
    nl_event_add_timer(&d->closing_ev, 0);

    return 0;
}


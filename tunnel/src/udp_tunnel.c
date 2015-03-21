#include "config.h"
#include "tunnel.h"

//static datagram_data_t *udp_addr2data;
//
//static datagram_data_t *find_datagram(struct sockaddr *saddr)
//{
//    datagram_data_t *d;
//    // FIXME: assume sockaddr_in here
//    struct sockaddr_in *addr = (struct sockaddr_in *)saddr;
//
//    for (d = udp_addr2data; d != NULL; d = d->next) {
//        if (d->peer.sin_addr.s_addr == addr->sin_addr.s_addr &&
//            d->peer.sin_port == addr->sin_port) {
//            return d;
//        }
//    }
//
//    return NULL;
//}
//
//static void push_datagram(datagram_data_t *d)
//{
//    d->next = udp_addr2data;
//    udp_addr2data = d;
//}
//
//static void erase_datagram(struct sockaddr_in *addr)
//{
//    datagram_data_t **last, *d;
//
//    last = &udp_addr2data;
//    for (d = udp_addr2data; d != NULL; d = d->next) {
//        if (d->peer.sin_addr.s_addr == addr->sin_addr.s_addr &&
//            d->peer.sin_port == addr->sin_port) {
//            *last = d->next;
//            return;
//        }
//
//        last = &d->next;
//    }
//
//    log_warning("#%d datagram not found", d->d.sock.fd);
//}
//
//static void on_udp_closed(nl_dgram_t *d)
//{
//    datagram_data_t *data;
//
//    data = d->data;
//
//    erase_datagram(&data->peer);
//    if (data->timeout.timer_set) {
//        nl_event_del_timer(&data->timeout);
//    }
//
//    log_debug("#%d datagram destroyed", d->sock.fd);
//
//    free(data);
//}
//
//// 10 min
//#define UDP_TIMEOUT (10 * 60 * 1000)
//
//static void on_udp_received(nl_dgram_t *d, nl_packet_t *p)
//{
//    datagram_data_t *data;
//
//    data = d->data;
//
//    memcpy(&p->addr, &data->peer, sizeof(data->peer));
//
//    if (s_con_obs) {
//        p->buf.buf = udp_con_decode(&data->con_o, p->buf.buf, &p->buf.len);
//    }
//    else if (s_acc_obs) {
//        p->buf.buf = udp_acc_encode(&data->con_o, p->buf.buf, &p->buf.len);
//    }
//
//    if (p->buf.buf != NULL) {
//        nl_dgram_send(data->acceptor, p);
//    }
//
//    nl_event_del_timer(&data->timeout);
//    nl_event_add_timer(&data->timeout, UDP_TIMEOUT);
//}
//
//void on_udp_sent(nl_dgram_t *d, nl_packet_t *p)
//{
//}
//
//void on_udp_timeout(nl_event_t *ev)
//{
//    datagram_data_t *d;
//
//    d = ev->data;
//
//    log_debug("#%d timeout", d->d.sock.fd);
//
//    nl_dgram_close(&d->d);
//}
//
//void on_udp_accepted(nl_dgram_t *d, nl_packet_t *p)
//{
//    int rc;
//    datagram_data_t *association;
//
//    association = find_datagram(&p->addr);
//    if (association == NULL) {
//        association = malloc(sizeof(datagram_data_t));
//        if (association == NULL) {
//            log_error("malloc() failed");
//            return;
//        }
//        memset(association, 0, sizeof(datagram_data_t));
//        udp_obscure(&association->acc_o);
//        udp_obscure(&association->con_o);
//
//        memcpy(&association->peer, &p->addr, sizeof(p->addr));
//        rc = nl_dgram(&association->d);
//        if (rc < 0) {
//            free(association);
//        }
//
//        association->d.on_received = on_udp_received;
//        association->d.on_sent = on_udp_sent;
//        association->d.on_closed = on_udp_closed;
//        association->d.data = association;
//        association->acceptor = d;
//
//        association->timeout.data = association;
//        association->timeout.handler = on_udp_timeout;
//
//        nl_event_add(&association->d.sock.rev);
//        //nl_event_add(&association->d.sock.wev);
//        push_datagram(association);
//    }
//
//    memcpy(&p->addr, d->data, sizeof(p->addr));
//
//    if (s_con_obs) {
//        p->buf.buf = udp_con_encode(&association->acc_o, p->buf.buf, &p->buf.len);
//    }
//    else if (s_acc_obs) {
//        p->buf.buf = udp_acc_decode(&association->acc_o, p->buf.buf, &p->buf.len);
//    }
//
//    // TODO: discard or send anyway?
//    if (p->buf.buf != NULL) {
//        nl_dgram_send(&association->d, p);
//    }
//
//    nl_event_del_timer(&association->timeout);
//    nl_event_add_timer(&association->timeout, UDP_TIMEOUT);
//}
//

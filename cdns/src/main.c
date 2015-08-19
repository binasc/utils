#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <string.h>

#include "dgram.h"
#include "config.h"
#include "dns.h"
#include "route.h"
#include "log.h"

#define TRAP_DNS "1.1.1.1"
#define TRAP_DNS_PORT 53
#define DNS_TIMEOUT 10000

static nl_dgram_t s_svr;
static nl_address_t s_addr;

static const char *ddns, *cdns;
static short ddns_port, cdns_port;

void on_closed(nl_dgram_t *d)
{
    free(d);
}

void on_timeout(nl_event_t *ev)
{
    nl_dgram_t *d = ev->data;
    nl_dgram_close(d);
}

void on_clean_received(nl_dgram_t *d, nl_packet_t *p)
{
    nl_packet_t tosend, *request;
    log_trace("on_clean_received");

    request = d->data;
    tosend.buf = p->buf;
    tosend.addr = request->addr;
    nl_dgram_send(&s_svr, &tosend);
    free(request);

    nl_dgram_close(d);
}

void on_dirty_received(nl_dgram_t *d, nl_packet_t *p)
{
    int rc, i;
    dmsg_t m;
    dmsg_rr rr;
    nl_packet_t tosend, *request;
    nl_dgram_t *resolver;

    log_trace("on_dirty_received");
    rc = dmsg_new(&m, p->buf.buf, p->buf.len);
    if (rc < 0) {
        log_error("dms_create() failed");
        return;
    }

    for (i = 0; i < dmsg_get_ancount(&m); i++) {
        rr = dmsg_get_an(&m, i);
        if (rr == NULL) {
        }

        if (dmsg_get_type(&m, rr) == DNS_RR_TYPE_A
                && dmsg_get_class(&m, rr) == DNS_RR_CLASS_IN) {
            if (test_addr(ntohl(*(uint32_t *)dmsg_get_rdata(&m, rr))) == 0) {
                break;
            }
        }
    }

    request = d->data;
    if (i < dmsg_get_ancount(&m)) {
        // send back
        log_debug("found! use dirty dns server result");
        tosend.buf = p->buf;
        tosend.addr = request->addr;
        nl_dgram_send(&s_svr, &tosend);
        free(request);
    }
    else {
        // continue resolve
        log_debug("not found! try resolving by clean dns server");
        resolver = malloc(sizeof(nl_dgram_t));
        if (resolver == NULL) {
        }
        rc = nl_dgram(resolver);
        if (rc < 0) {
        }
        resolver->on_received = on_clean_received;
        resolver->on_closed = on_closed;
        resolver->data = request;

        nl_dgram_resume_receiving(resolver);

        tosend.buf = request->buf;
        nl_address_setname(&tosend.addr, cdns);
        nl_address_setport(&tosend.addr, cdns_port);
        nl_dgram_send(resolver, &tosend);

        resolver->timeout_ev.handler = on_timeout;
        resolver->timeout_ev.data = resolver;
        nl_event_add_timer(&resolver->timeout_ev, DNS_TIMEOUT);
    }

    nl_dgram_close(d);
    //dmsg_debug_print(&m);
}

void on_trap_timeout(nl_event_t *ev)
{
    int rc;
    nl_dgram_t *d, *resolver;
    nl_packet_t tosend, *request;

    d = ev->data;
    request = d->data;

    log_trace("on_trap_timeout");
    resolver = malloc(sizeof(nl_dgram_t));
    if (resolver == NULL) {
    }
    rc = nl_dgram(resolver);
    if (rc < 0) {
    }
    resolver->on_received = on_dirty_received;
    resolver->on_closed = on_closed;
    resolver->data = request;

    nl_dgram_resume_receiving(resolver);

    tosend.buf = request->buf;
    nl_address_setname(&tosend.addr, ddns);
    nl_address_setport(&tosend.addr, ddns_port);
    nl_dgram_send(resolver, &tosend);

    resolver->timeout_ev.handler = on_timeout;
    resolver->timeout_ev.data = resolver;
    nl_event_add_timer(&resolver->timeout_ev, DNS_TIMEOUT);

    nl_dgram_close(d);
}

void on_trap_received(nl_dgram_t *d, nl_packet_t *p)
{
    int rc;
    nl_dgram_t *resolver;
    nl_packet_t tosend, *request;

    request = d->data;

    log_trace("on_trap_received");
    resolver = malloc(sizeof(nl_dgram_t));
    if (resolver == NULL) {
    }
    rc = nl_dgram(resolver);
    if (rc < 0) {
    }
    resolver->on_received = on_clean_received;
    resolver->on_closed = on_closed;
    resolver->data = request;

    nl_dgram_resume_receiving(resolver);

    tosend.buf = request->buf;
    nl_address_setname(&tosend.addr, cdns);
    nl_address_setport(&tosend.addr, cdns_port);
    nl_dgram_send(resolver, &tosend);

    resolver->timeout_ev.handler = on_timeout;
    resolver->timeout_ev.data = resolver;
    nl_event_add_timer(&resolver->timeout_ev, 10000);

    nl_dgram_close(d);
}

void on_svr_received(nl_dgram_t *d, nl_packet_t *p)
{
    int rc;
    dmsg_t m;
    nl_packet_t tosend, *request;
    char name[256];
    nl_dgram_t *resolver;
    dmsg_qd qd;

    rc = dmsg_new(&m, p->buf.buf, p->buf.len);
    if (rc < 0) {
        log_error("dms_create() failed");
        return;
    }

    //dmsg_debug_print(&m);
    qd = dmsg_get_qd(&m, 0);
    if (qd == NULL) {
    }
    dmsg_get_qname(&m, qd, name);
    log_debug("name: %s", name);
    dmsg_delete(&m);

    request = malloc(sizeof(nl_packet_t) + p->buf.len);
    if (request == NULL) {
    }
    request->addr = p->addr;
    request->buf.len = p->buf.len;
    request->buf.buf = ((char *)request + sizeof(nl_packet_t));
    memcpy(request->buf.buf, p->buf.buf, p->buf.len);

    resolver = malloc(sizeof(nl_dgram_t));
    if (resolver == NULL) {
    }
    rc = nl_dgram(resolver);
    if (rc < 0) {
    }
    resolver->on_received = on_trap_received;
    resolver->on_closed = on_closed;
    resolver->data = request;

    nl_dgram_resume_receiving(resolver);

    tosend.buf = p->buf;
    nl_address_setname(&tosend.addr, TRAP_DNS);
    nl_address_setport(&tosend.addr, TRAP_DNS_PORT);
    nl_dgram_send(resolver, &tosend);

    resolver->timeout_ev.handler = on_trap_timeout;
    resolver->timeout_ev.data = resolver;
    // TODO: optimise by network
    nl_event_add_timer(&resolver->timeout_ev, 10);
}

int main(int argc, char *argv[])
{
    int rc;
    const char *fname;

    if (argc < 5) {
        log_error("usage: {db} {dirty_dns} {port} {clean_dns} {port}");
        return -1;
    }

    fname = argv[1];
    ddns = argv[2];
    sscanf(argv[3], "%hu", &ddns_port);
    cdns = argv[4];
    sscanf(argv[5], "%hu", &cdns_port);

    log_debug("ddns: %s:%hu, cdns: %s:%hu", ddns, ddns_port, cdns, cdns_port);

    rc = route_init(fname);
    if (rc < 0) {
        return -1;
    }

    nl_event_init();

    rc = nl_dgram(&s_svr);
    if (rc == -1) {
        return -1;
    }
    s_svr.on_received = on_svr_received;
    s_svr.data = NULL;

    nl_address_setname(&s_addr, "0.0.0.0");
    nl_address_setport(&s_addr, 53);

    rc = nl_dgram_bind(&s_svr, &s_addr);
    if (rc < 0) {
        return -1;
    }

    nl_dgram_resume_receiving(&s_svr);

    nl_process_loop();

    return 0;
}


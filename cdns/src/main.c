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
#include "address.h"

#define TRAP_DNS "8.8.4.1"
#define TRAP_DNS_PORT 53

// TODO: optimise by network
#define TDNS_TIMEOUT 50
#define DDNS_TIMEOUT 100
#define CDNS_TIMEOUT 1000

static nl_dgram_t s_svr;
static nl_address_t s_addr;

static const char *ddns, *cdns;
static short ddns_port, cdns_port;

void on_closed(nl_dgram_t *d)
{
    log_trace("on_closed");
    // request
    if (d->data != NULL) {
        free(d->data);
        d->data = NULL;
    }
    free(d);
}

void on_last_timeout(nl_event_t *ev)
{
    log_trace("on_last_timeout");
    nl_dgram_close(ev->data);
}

void on_last_received(nl_dgram_t *d, nl_packet_t *p)
{
    nl_packet_t tosend, *request;
    log_trace("on_last_received");


    nl_address_t addr;
    nl_address_setname(&addr, cdns);
    nl_address_setport(&addr, cdns_port);
    if (nl_address_equal(&p->addr, &addr) != 0) {
        log_debug("ignore packet from unexpected source");
        return;
    }

    request = d->data;
    tosend.buf = p->buf;
    tosend.addr = request->addr;
    nl_dgram_send(&s_svr, &tosend);

    nl_dgram_close(d);
}

void on_clean_timeout(nl_event_t *ev)
{
    nl_dgram_t *resolver;
    nl_packet_t tosend, *request;
    log_trace("on_clean_timeout");

    resolver = ev->data;
    request = resolver->data;

    resolver->on_received = on_last_received;
    resolver->on_closed = on_closed;
    resolver->data = request;

    nl_dgram_resume_receiving(resolver);

    tosend.buf = request->buf;
    nl_address_setname(&tosend.addr, ddns);
    nl_address_setport(&tosend.addr, ddns_port);
    nl_dgram_send(resolver, &tosend);

    resolver->timeout_ev.handler = on_last_timeout;
    resolver->timeout_ev.data = resolver;
    nl_event_del_timer(&resolver->timeout_ev);
    nl_event_add_timer(&resolver->timeout_ev, DDNS_TIMEOUT);
}

void on_dirty_received(nl_dgram_t *d, nl_packet_t *p)
{
    int rc, i;
    dmsg_t m;
    dmsg_rr rr;
    nl_packet_t tosend, *request;
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

        nl_dgram_close(d);
    }
    else {
        // continue resolve
        nl_dgram_t *resolver;
        log_debug("not found! try resolving by clean dns server");
        resolver = d;

        resolver->on_received = on_last_received;
        resolver->on_closed = on_closed;
        resolver->data = request;

        nl_dgram_resume_receiving(resolver);

        tosend.buf = request->buf;
        nl_address_setname(&tosend.addr, cdns);
        nl_address_setport(&tosend.addr, cdns_port);
        nl_dgram_send(resolver, &tosend);

        resolver->timeout_ev.handler = on_clean_timeout;
        resolver->timeout_ev.data = resolver;
        nl_event_del_timer(&resolver->timeout_ev);
        nl_event_add_timer(&resolver->timeout_ev, CDNS_TIMEOUT);
    }
}

void on_trap_timeout(nl_event_t *ev)
{
    nl_dgram_t *resolver;
    nl_packet_t tosend, *request;
    log_trace("on_trap_timeout");

    resolver = ev->data;
    request = resolver->data;

    resolver->on_received = on_dirty_received;
    resolver->on_closed = on_closed;
    resolver->data = request;

    nl_dgram_resume_receiving(resolver);

    tosend.buf = request->buf;
    nl_address_setname(&tosend.addr, ddns);
    nl_address_setport(&tosend.addr, ddns_port);
    nl_dgram_send(resolver, &tosend);

    resolver->timeout_ev.handler = on_last_timeout;
    resolver->timeout_ev.data = resolver;
    nl_event_del_timer(&resolver->timeout_ev);
    nl_event_add_timer(&resolver->timeout_ev, DDNS_TIMEOUT);
}

void on_trap_received(nl_dgram_t *d, nl_packet_t *p)
{
    nl_dgram_t *resolver;
    nl_packet_t tosend, *request;
    log_trace("on_trap_received");

    resolver = d;
    request = resolver->data;

    resolver->on_received = on_last_received;
    resolver->on_closed = on_closed;
    resolver->data = request;

    nl_dgram_resume_receiving(resolver);

    tosend.buf = request->buf;
    nl_address_setname(&tosend.addr, cdns);
    nl_address_setport(&tosend.addr, cdns_port);
    nl_dgram_send(resolver, &tosend);

    resolver->timeout_ev.handler = on_clean_timeout;
    resolver->timeout_ev.data = resolver;
    nl_event_del_timer(&resolver->timeout_ev);
    nl_event_add_timer(&resolver->timeout_ev, CDNS_TIMEOUT);
}

void on_svr_received(nl_dgram_t *d, nl_packet_t *p)
{
    int rc;
    dmsg_t m;
    dmsg_qd qd;
    char name[256];
    log_trace("on_svr_received");

    nl_packet_t tosend, *request;
    nl_dgram_t *resolver;

    rc = dmsg_new(&m, p->buf.buf, p->buf.len);
    if (rc < 0) {
        log_error("dmsg_new() failed");
        return;
    }

    //dmsg_debug_print(&m);
    name[0] = 0;
    qd = dmsg_get_qd(&m, 0);
    if (qd == NULL) {
        log_warning("dmsg_get_qd() get nothing");
    }
    else {
        dmsg_get_qname(&m, qd, name);
    }
    log_debug("name: %s", name);
    dmsg_delete(&m);

    request = malloc(sizeof(nl_packet_t) + p->buf.len);
    if (request == NULL) {
        log_error("malloc() failed");
        return;
    }
    request->addr = p->addr;
    request->buf.len = p->buf.len;
    request->buf.buf = ((char *)request + sizeof(nl_packet_t));
    memcpy(request->buf.buf, p->buf.buf, p->buf.len);

    resolver = malloc(sizeof(nl_dgram_t));
    if (resolver == NULL) {
        log_error("malloc() failed");
        free(request);
        return;
    }
    rc = nl_dgram(resolver);
    if (rc < 0) {
        log_error("nl_dgram() failed");
        free(request);
        free(resolver);
        return;
    }

    int isGname = 0;
    int isRDNS = 0;
    int isDomestic = 0;
    size_t slen = strlen(name);
#define GNAME "google.com."
#define GHKNAME "google.com.hk."
#define GJPNAME "google.co.jp."
    if ((slen >=  strlen(GNAME) && strcmp(name + (slen - strlen(GNAME)), GNAME) == 0) ||
        (slen >=  strlen(GHKNAME) && strcmp(name + (slen - strlen(GHKNAME)), GHKNAME) == 0) ||
        (slen >=  strlen(GJPNAME) && strcmp(name + (slen - strlen(GJPNAME)), GJPNAME) == 0)) {
        isGname = 1;
    }
#define RDNS "in-addr.arpa."
    else if (slen >=  strlen(RDNS) && strcmp(name + (slen - strlen(RDNS)), RDNS) == 0) {
        isRDNS = 1;
        int i = 0, pos;
        uint32_t val, ipv4 = 0;

        for (pos = 3; pos >= 0; pos--) {
            val = 0;
            while (i < slen && name[i] != '.') {
                val *= 10;
                val += name[i] - '0';
                i++;
            }
            ipv4 += (val << (pos * 8));
            i++;
        }

        isDomestic = test_addr(ntohl(ipv4)) == 0 ? 1 : 0;
    }

    if (isGname || (isRDNS && !isDomestic)) {
        log_debug("resolve google name or abroad rdns");
        resolver->on_received = on_last_received;
        resolver->on_closed = on_closed;
        resolver->data = request;

        nl_dgram_resume_receiving(resolver);

        tosend.buf = p->buf;
        nl_address_setname(&tosend.addr, cdns);
        nl_address_setport(&tosend.addr, cdns_port);
        nl_dgram_send(resolver, &tosend);

        resolver->timeout_ev.handler = on_clean_timeout;
        resolver->timeout_ev.data = resolver;
        nl_event_add_timer(&resolver->timeout_ev, CDNS_TIMEOUT);
    }
    else if (isRDNS) {
        log_debug("domestic rdns");
        resolver->on_received = on_last_received;
        resolver->on_closed = on_closed;
        resolver->data = request;

        nl_dgram_resume_receiving(resolver);

        tosend.buf = p->buf;
        nl_address_setname(&tosend.addr, ddns);
        nl_address_setport(&tosend.addr, ddns_port);
        nl_dgram_send(resolver, &tosend);

        resolver->timeout_ev.handler = on_clean_timeout;
        resolver->timeout_ev.data = resolver;
        nl_event_add_timer(&resolver->timeout_ev, DDNS_TIMEOUT);
    }
    else {
        log_debug("resolve non google name");
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
        nl_event_add_timer(&resolver->timeout_ev, TDNS_TIMEOUT);
    }
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


#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>

#include "dgram.h"
#include "config.h"
#include "dns.h"
#include "log.h"

static nl_dgram_t s_svr;
static nl_address_t s_addr;

static nl_dgram_t s_cli;

void on_cli_received(nl_dgram_t *d, nl_packet_t *p)
{
    int rc;
    dmsg_t m;
    rc = dmsg_new(&m, p->buf.buf, p->buf.len);
    if (rc < 0) {
        log_error("dms_create() failed");
        return;
    }

    dmsg_debug_print(&m);
}

void on_svr_received(nl_dgram_t *d, nl_packet_t *p)
{
    int rc;
    dmsg_t m;
    nl_packet_t tosend;

    rc = dmsg_new(&m, p->buf.buf, p->buf.len);
    if (rc < 0) {
        log_error("dms_create() failed");
        return;
    }

    dmsg_debug_print(&m);

    tosend.buf = p->buf;
    nl_address_setname(&tosend.addr, "8.8.8.8");
    nl_address_setport(&tosend.addr, 53);
    nl_dgram_send(&s_cli, &tosend);
}

int main(int argc, char *argv[])
{
    int rc;

    //srand(time(NULL));

    nl_event_init();

    rc = nl_dgram(&s_svr);
    if (rc == -1) {
        return -1;
    }
    s_svr.on_received = on_svr_received;
    s_svr.data = NULL;

    nl_address_setname(&s_addr, "0.0.0.0");
    nl_address_setport(&s_addr, 10053);

    rc = nl_dgram_bind(&s_svr, &s_addr);
    if (rc < 0) {
        return -1;
    }

    nl_dgram_resume_receiving(&s_svr);

    rc = nl_dgram(&s_cli);
    if (rc == -1) {
        return -1;
    }
    s_cli.on_received = on_cli_received;
    s_cli.data = NULL;

    nl_dgram_resume_receiving(&s_cli);

    nl_process_loop();

    return 0;
}


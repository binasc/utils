#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>

#include "dgram.h"
#include "config.h"
#include "obscure.h"
#include "log.h"

static nl_dgram_t s_svr;
static nl_address_t s_addr;

void on_svr_received(nl_dgram_t *d, nl_packet_t *p)
{
    printf("received");
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

    nl_process_loop();

    return 0;
}


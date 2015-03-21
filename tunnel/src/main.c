#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>

#include "config.h"
#include "obscure.h"
#include "tunnel.h"
#include "log.h"

static nl_stream_t s_acceptors[10];
static acceptor_data_t s_acceptor_data[10];

void on_accepted(nl_stream_t *c);

int main(int argc, char *argv[])
{
    int rc;

    rc = tun_options(argc, argv);
    if (rc == -1) {
        return -1;
    }

    srand(time(NULL));

    nl_event_init();

    if (tun_is_udp_mode()) {
        //nl_dgram_t d;

        //rc = nl_dgram(&d);
        //if (rc < 0) {
        //    return -1;
        //}

        //// TODO: udp support multiple tunnels
        //rc = nl_dgram_bind(&d, &s_laddrs[0].addr);
        //if (rc < 0) {
        //    return -1;
        //}

        //d.on_received = on_udp_accepted;
        //d.on_sent = on_udp_sent;
        //d.data = &s_raddrs[0].addr;

        //nl_event_add(&d.sock.rev);
        //nl_event_add(&d.sock.wev);
    }
    else {
        int i;
        address_tuple_t *t;

        for (i = 0; i < tun_num_address_tuple(); i++) {
            t = tun_get_address_tuple(i);
            if (t == NULL) {
                return -1;
            }

            rc = nl_stream(&s_acceptors[i]);
            if (rc == -1) {
                return -1;
            }

            s_acceptor_data[i].from = &t->from;
            s_acceptor_data[i].via = &t->via;
            s_acceptor_data[i].to = &t->to;

            s_acceptors[i].cbs.on_accepted = on_accepted;
            s_acceptors[i].data = &s_acceptor_data[i];

            rc = nl_stream_listen(&s_acceptors[i], &t->from, 10);
            if (rc < 0) {
                return -1;
            }
        }
    }

    nl_process_loop();

    return 0;
}


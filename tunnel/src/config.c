#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"

static int s_obscure;

static int s_accept;
static int s_connect;

static int s_udp_mode;

static char argC[256];
static char argA[256];
static address_tuple_t s_address[10];
static int s_naddress;

static int parse_hostport(char *arg, nl_address_t *addr)
{
    int rc;
    char *colon;

    colon = strchr(arg, ':');
    if (colon == NULL) {
        log_error("invalid host:port - %s", arg);
        return -1;
    }

    *colon = 0;

    rc = nl_address_setname(addr, arg);
    if (rc < 0) {
        return -1;
    }

    rc = nl_address_setport(addr, atoi(colon + 1));
    if (rc < 0) {
        return -1;
    }

    return 0;
}

static int parse_address(char *arg)
{
    int rc, ending;
    char *end;

    while (1) {
        ending = 0;
        end = strchr(arg, '/');
        if (end != NULL) {
            *end = 0;
        }
        else {
            ending = 1;
        }

        end = strchr(strchr(arg, ':') + 1, ':');
        if (end != NULL) {
            *end = 0;
        }
        rc = parse_hostport(arg, &s_address[s_naddress].from);
        if (rc == -1) {
            return -1;
        }
        if (s_connect && end == NULL) {
            return -1;
        }
        arg = end + 1;

        if (s_connect) {
            end = strchr(strchr(arg, ':') + 1, ':');
            if (end == NULL) {
                return -1;
            }
            *end = 0;
            rc = parse_hostport(arg, &s_address[s_naddress].via);
            if (rc == -1) {
                return -1;
            }
            arg = end + 1;

            end = strchr(strchr(arg, ':') + 1, ':');
            if (end != NULL) {
                if (ending) {
                    return -1;
                }
                *end = 0;
            }
            rc = parse_hostport(arg, &s_address[s_naddress].to);
            if (rc == -1) {
                return -1;
            }
            arg = end + 1;
        }
        s_naddress++;
        if (ending) {
            return 0;
        }
    }

    /* should never reach here */
    return -1;
}

int tun_options(int argc, char *argv[])
{
    int rc, opt;
    int version = 0;

    while ((opt = getopt(argc, argv, "vA:C:Ou")) != EOF) {
        switch (opt) {
            case 'A':
                s_accept = 1;
                strcpy(argA, optarg);
                rc = parse_address(argA);
                if (rc == -1) {
                    log_error("invalid option -- '%s'", optarg);
                    return -1;
                }
                break;
            case 'C':
                s_connect = 1;
                strcpy(argC, optarg);
                rc = parse_address(argC);
                if (rc == -1) {
                    log_error("invalid option -- '%s'", optarg);
                    return -1;
                }
                break;
            case 'O':
                s_obscure = 1;
                break;
            case 'u':
                s_udp_mode = 1;
                break;
            case 'v' :
                version = 1;
                break;
            default:
                log_error("invalid option -- '%c'", opt);
                return -1;
        }
    }

    if (version) {
        printf("tunnel build in %s %s\n", __DATE__, __TIME__);
        exit(0);
    }

    return 0;
}

int tun_is_accept_side()
{
    return s_accept;
}

int tun_is_connect_side()
{
    return s_connect;
}

int tun_need_obscure()
{
    return s_obscure;
}

int tun_is_udp_mode()
{
    return s_udp_mode;
}

int tun_num_address_tuple()
{
    return s_naddress;
}

address_tuple_t *tun_get_address_tuple(int num)
{
    if (num < s_naddress) {
        return &s_address[num];
    }

    return NULL;
}


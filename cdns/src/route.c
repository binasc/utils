#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "log.h"

struct domain
{
    uint32_t addr;
    uint32_t mask;
};

static int compar(const void *p1, const void *p2)
{
    const struct domain *lhd, *rhd;

    lhd = p1;
    rhd = p2;

    if (lhd->addr == rhd->addr) {
        if (lhd->mask == rhd->mask) {
            return 0;
        }
        return lhd->mask < rhd->mask ? -1 : 1;
    }
    return lhd->addr < rhd->addr ? -1 : 1;
}

static struct domain domains[10000];
static size_t domain_count;

static char line[256];

static void on_read_line(const char *line)
{
    static char addr[256];
    const char *slash;
    struct domain *d;

    slash = strchr(line, '/');
    if (slash == NULL) {
        log_error("invalid line: %s", line);
        return;
    }
    memcpy(addr, line, slash - line);
    addr[slash - line] = 0;

    d = &domains[domain_count++];

    d->addr = ntohl((uint32_t)inet_addr(addr));

    sscanf(slash + 1, "%d", &d->mask);
}

static uint32_t masks[] = 
{
    0x00000000,
    0x80000000,
    0xc0000000,
    0xe0000000,
    0xf0000000,
    0xf8000000,
    0xfc000000,
    0xfe000000,
    0xff000000,
    0xff800000,
    0xffc00000,
    0xffe00000,
    0xfff00000,
    0xfff80000,
    0xfffc0000,
    0xfffe0000,
    0xffff0000,
    0xffff8000,
    0xffffc000,
    0xffffe000,
    0xfffff000,
    0xfffff800,
    0xfffffc00,
    0xfffffe00,
    0xffffff00,
    0xffffff80,
    0xffffffc0,
    0xffffffe0,
    0xfffffff0,
    0xfffffff8,
    0xfffffffc,
    0xfffffffe,
    0xffffffff,
};

int route_init(const char *fname)
{
    char ch;
    size_t count = 0;
    FILE *fp = fopen(fname, "r");

    if (fp == NULL) {
        log_error("fopen() failed");
        return -1;
    }

    while ((ch = getc(fp)) != EOF) {
        if (ch == '\n') {
            line[count] = 0;
            if (count > 0) {
                on_read_line(line);
            }
            count = 0;
            continue;
        }
        line[count++] = ch;
    }

    log_debug("read %zu records", domain_count);
    qsort(domains, domain_count, sizeof(struct domain), compar);

    fclose(fp);

    return 0;
}

int test_addr(uint32_t addr)
{
    int i;

    for (i = 0; i < domain_count; i++) {
        struct domain *d = &domains[i];
        if ((addr & masks[d->mask]) == d->addr) {
            struct in_addr a1;
            a1.s_addr = htonl(d->addr);
            log_debug("found: %s/%u", inet_ntoa(a1), d->mask);
            return 0;
        }
    }
    struct in_addr a0;
    a0.s_addr = htonl(addr);
    log_debug("not found: %s", inet_ntoa(a0));

    return -1;
}


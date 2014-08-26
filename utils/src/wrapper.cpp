#include "utils.h"
#include "utils.hpp"

#include "configure.h"
#include "configure.cpp"

//#include "nlog.h"
#include "nlog.hpp"

#include <map>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

using namespace utils;
using namespace std;

void utils_T_daemon(const char *path)
{
    T::daemon(path);
}

int utils_T_lock_wait(const char *fname)
{
    return T::lock_wait(fname);
}

void utils_T_partner(const char *lockname, char *argv[])
{
    T::partner(lockname, argv);
}


configure_t *utils_new_configure()
{
    configure_t *ret;
    try {
        ret = (configure_t *)new Configure();
    }
    catch (...) {
        return NULL;
    }
    return ret;
}

void utils_delete_configure(configure_t *c)
{
    delete (Configure *)c;
}

int uitls_configure_load(configure_t *c, const char *file)
{
    return ((Configure *)c)->load(file);
}

int utils_configure_get_single_str(configure_t *c, const char *section, const char *key, char *val, size_t len)
{
    int rc;
    string v;
    rc = ((Configure *)c)->get_single(section, key, v);
    if (rc == 0) {
        if (len < v.size()) {
            rc = -1;
        }
        else {
            strcpy(val, v.c_str());
        }
    }
    return rc;
}

int utils_configure_get_single_long(configure_t *c, const char *section, const char *key, long *val)
{
    return ((Configure *)c)->get_single(section, key, *val);
}

int utils_nlog_init(const char *rip, short rport, short lport, int level)
{
    char tmp[24];

    map<string, string> linfo;
    linfo["rip"] = rip;
    sprintf(tmp, "%d", rport);
    linfo["rport"] = tmp;
    linfo["lip"] = "0.0.0.0";
    sprintf(tmp, "%d", lport);
    linfo["lport"] = tmp;
    sprintf(tmp, "%d", level);
    linfo["level"] = tmp;
    return LOG_INIT(linfo) ? 0 : -1;
}

int utils_nlog_log(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = NLog::global_log.log((LogLevel)level, fmt, ap); // not safe
    va_end(ap);
    return ret;
}


#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

void utils_T_daemon(const char *path);

int utils_T_lock_wait(const char *fname);

void utils_T_partner(const char *lockname, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif


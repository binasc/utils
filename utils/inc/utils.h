#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

void utils_daemon(const char *path);

void utils_partner(const char *lockname, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif


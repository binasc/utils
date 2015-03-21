#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 6
#endif

#if LOG_LEVEL > 0
#define log_fatal(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#else
#define log_fatal(fmt, ...)
#endif

#if LOG_LEVEL > 1
#define log_error(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#else
#define log_error(fmt, ...)
#endif

#if LOG_LEVEL > 2
#define log_warning(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#else
#define log_warning(fmt, ...)
#endif

#if LOG_LEVEL > 3
#define log_info(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#else
#define log_info(fmt, ...)
#endif

#if LOG_LEVEL > 4
#define log_debug(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#else
#define log_debug(fmt, ...)
#endif

#if LOG_LEVEL > 5
#define log_trace(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#else
#define log_trace(fmt, ...)
#endif

#endif


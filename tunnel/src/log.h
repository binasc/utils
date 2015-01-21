#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

#define log_fatal(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#define log_error(fmt, ...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)
#define log_warning(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#define log_debug(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)
#define log_trace(fmt, ...) printf(fmt"\n", ##__VA_ARGS__)

#endif


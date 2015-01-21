#ifndef __NLOG_H__
#define __NLOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#define L_FATAL 0       ///< 致命错误，程序不得不停止
#define L_ERROR 1       ///< 故障，如网络连接错误
#define L_WARN  2       ///< 警告，不影响业务的故障
#define L_INFO  3       ///< 业务信息记录
//#define L_RESERVD     ///< 程序流程跟踪
#define L_DEBUG 4       ///< 调试信息
#define L_TRACE 5       ///< 程序最详细信息
#define L_LEVEL_MAX 6

int utils_nlog_init(const char *rip, short rport, short lport, int level);

int utils_nlog_log(int level, const char *fmt, ...);

#define LOG_FATAL(fmt, ...) utils_nlog_log(L_FATAL, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) utils_nlog_log(L_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) utils_nlog_log(L_WARN, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) utils_nlog_log(L_INFO, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) utils_nlog_log(L_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) utils_nlog_log(L_TRACE, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif



#ifndef NLOG_H_
#define NLOG_H_

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <map>
#include <netinet/in.h>
#include <string>
using namespace std;

extern "C" {
    int utils_nlog_log(int level, const char *fmt, ...);
}

namespace utils {

enum LogLevel
{
    L_FATAL = 0,    ///< 致命错误，程序不得不停止
    L_ERROR,        ///< 故障，如网络连接错误
    L_WARN,         ///< 警告，不影响业务的故障
    L_INFO,         ///< 业务信息记录
    L_DEBUG,        ///< 调试信息
    L_TRACE,        ///< 程序最详细信息
    L_LEVEL_MAX
};

class NLog
{
    public:
        /**
          构造Log对象
         */
        NLog();

        /**
          析构Log对象

          析构前将关闭日志文件
          @see close()
         */
        ~NLog();

    public:
        friend int ::utils_nlog_log(int level, const char *fmt, ...);

        bool init(map<string,string> linfo);
        /**
          设置最大日志等级

          只对后续输出的日志有效
          @param level 新的级别上限
          @return 0 成功 -1 出现错误
         */
        int set_max_level(LogLevel level);

        /**
          是否输出到 usec级别
         **/
        int set_usec(bool in_enable_usec);

        /**
          是否打开16进制pack打印功能，默认为打开
         **/
        int set_pack_print(bool in_enable_pack_print);

        /**
          获取最大日志等级

          @return 最大日志等级
         */
        LogLevel get_max_level() {return max_level_;}

        int open();
#ifdef WIN32	// for windows
#define CHECK_FORMAT(i, j)
#else			// for linux(gcc)
#define CHECK_FORMAT(i, j) __attribute__((format(printf, i, j)))
#endif

        /**
          输出一条日志记录

          @param level 日志等级
          @param fmt 格式化字符串
          @return 0 成功 -1 出现错误
         */
        int log(LogLevel level, const char * fmt, ...) CHECK_FORMAT(3, 4);

        /// 输出一条FATAL日志记录
        int log_fatal(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// 输出一条ERROR日志记录
        int log_error(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// 输出一条WARN日志记录
        int log_warn(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// 输出一条INFO日志记录
        int log_info(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// 输出一条TRACE日志记录
        int log_trace(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// 输出一条DEBUG日志记录
        int log_debug(const char * fmt, ...) CHECK_FORMAT(2, 3);

#undef CHECK_FORMAT

        /**
          用十六进制dump一段数据

          @param data 数据首地址
          @param len 数据长度
          @param level 日志等级
          @return 0 成功 -1 出现错误
         */
        int log_hex(unsigned char * data, size_t len, LogLevel level);
        int log_hex_prefix(unsigned char * prefix, unsigned char * data, size_t len, LogLevel level);
        bool  strformatreplace(char * srcstr, char * desstr);
    public:
        /// 全局日志对象
        static NLog global_log;

    private:

        /**
          输出一条日志记录

          @param level 日志等级
          @param fmt 格式化字符串
          @param ap 格式化参数
          @return 0 成功 -1 出现错误
          @see log()
         */
        int vlog(int level, const char* fmt, va_list ap);

    private:
        /// 不同日志级别的颜色以及关键字描述
        static char level_str_[L_LEVEL_MAX][64];
        static char level_str_usec_[L_LEVEL_MAX][64];
        static const size_t MAX_LOG_BUF_SIZE = 1024*9;
    private:

        /// 日志级别
        LogLevel max_level_;

        /// 日志文件文件描述符
        int sock_;

        /// 远程地址
        struct sockaddr_in rmoteaddr_;

        /// 今天开始时刻
        time_t mid_night_;

        bool enable_usec;

        bool enable_pack_print;
};

}


#define LOG_FATAL utils::NLog::global_log.log_fatal
#define LOG_ERROR utils::NLog::global_log.log_error
#define LOG_WARN utils::NLog::global_log.log_warn
#define LOG_INFO utils::NLog::global_log.log_info
#define LOG_TRACE utils::NLog::global_log.log_trace
#define LOG_DEBUG utils::NLog::global_log.log_debug

#define LOG_HEX(data, len, level) utils::NLog::global_log.log_hex((unsigned char *)(data), (len), (level))
#define LOG_HEX_PREFIX(prefix, data, len, level) utils::NLog::global_log.log_hex_prefix((unsigned char *)(prefix), (unsigned char *)(data), (len), (level))

#define LOG_INIT(loginfo) \
    utils::NLog::global_log.init(loginfo)

#define LOG_SET_LEVEL(level) utils::NLog::global_log.set_max_level((utils::LogLevel)(level))

#define LOG_GET_LEVEL() utils::NLog::global_log.get_max_level()

#define LOG_SET_USEC(enable_usec) utils::NLog::global_log.set_usec(enable_usec)

#define LOG_SET_PACK_PRINT(in_enable_pack_print) utils::NLog::global_log.set_pack_print(in_enable_pack_print)


#endif /* NLOG_H_ */

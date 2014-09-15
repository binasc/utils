
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
    L_FATAL = 0,    ///< �������󣬳��򲻵ò�ֹͣ
    L_ERROR,        ///< ���ϣ����������Ӵ���
    L_WARN,         ///< ���棬��Ӱ��ҵ��Ĺ���
    L_INFO,         ///< ҵ����Ϣ��¼
    L_DEBUG,        ///< ������Ϣ
    L_TRACE,        ///< ��������ϸ��Ϣ
    L_LEVEL_MAX
};

class NLog
{
    public:
        /**
          ����Log����
         */
        NLog();

        /**
          ����Log����

          ����ǰ���ر���־�ļ�
          @see close()
         */
        ~NLog();

    public:
        friend int ::utils_nlog_log(int level, const char *fmt, ...);

        bool init(map<string,string> linfo);
        /**
          ���������־�ȼ�

          ֻ�Ժ����������־��Ч
          @param level �µļ�������
          @return 0 �ɹ� -1 ���ִ���
         */
        int set_max_level(LogLevel level);

        /**
          �Ƿ������ usec����
         **/
        int set_usec(bool in_enable_usec);

        /**
          �Ƿ��16����pack��ӡ���ܣ�Ĭ��Ϊ��
         **/
        int set_pack_print(bool in_enable_pack_print);

        /**
          ��ȡ�����־�ȼ�

          @return �����־�ȼ�
         */
        LogLevel get_max_level() {return max_level_;}

        int open();
#ifdef WIN32	// for windows
#define CHECK_FORMAT(i, j)
#else			// for linux(gcc)
#define CHECK_FORMAT(i, j) __attribute__((format(printf, i, j)))
#endif

        /**
          ���һ����־��¼

          @param level ��־�ȼ�
          @param fmt ��ʽ���ַ���
          @return 0 �ɹ� -1 ���ִ���
         */
        int log(LogLevel level, const char * fmt, ...) CHECK_FORMAT(3, 4);

        /// ���һ��FATAL��־��¼
        int log_fatal(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// ���һ��ERROR��־��¼
        int log_error(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// ���һ��WARN��־��¼
        int log_warn(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// ���һ��INFO��־��¼
        int log_info(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// ���һ��TRACE��־��¼
        int log_trace(const char * fmt, ...) CHECK_FORMAT(2, 3);

        /// ���һ��DEBUG��־��¼
        int log_debug(const char * fmt, ...) CHECK_FORMAT(2, 3);

#undef CHECK_FORMAT

        /**
          ��ʮ������dumpһ������

          @param data �����׵�ַ
          @param len ���ݳ���
          @param level ��־�ȼ�
          @return 0 �ɹ� -1 ���ִ���
         */
        int log_hex(unsigned char * data, size_t len, LogLevel level);
        int log_hex_prefix(unsigned char * prefix, unsigned char * data, size_t len, LogLevel level);
        bool  strformatreplace(char * srcstr, char * desstr);
    public:
        /// ȫ����־����
        static NLog global_log;

    private:

        /**
          ���һ����־��¼

          @param level ��־�ȼ�
          @param fmt ��ʽ���ַ���
          @param ap ��ʽ������
          @return 0 �ɹ� -1 ���ִ���
          @see log()
         */
        int vlog(int level, const char* fmt, va_list ap);

    private:
        /// ��ͬ��־�������ɫ�Լ��ؼ�������
        static char level_str_[L_LEVEL_MAX][64];
        static char level_str_usec_[L_LEVEL_MAX][64];
        static const size_t MAX_LOG_BUF_SIZE = 1024*9;
    private:

        /// ��־����
        LogLevel max_level_;

        /// ��־�ļ��ļ�������
        int sock_;

        /// Զ�̵�ַ
        struct sockaddr_in rmoteaddr_;

        /// ���쿪ʼʱ��
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

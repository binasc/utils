//nlog.cpp

#include "nlog.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#ifdef WIN32

#if _MSC_VER <= 1400
#define vsnprintf _vsnprintf
#define snprintf _snprintf
#endif

#else

#include <unistd.h>

#endif

#ifdef __MACH__
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

namespace utils
{

bool NLog:: init(map<string,string> linfo)
{
    struct sockaddr_in localaddr;
    memset(&localaddr, 0, sizeof(localaddr));
    if(linfo.end() != linfo.find("lip"))
    {
        localaddr.sin_addr.s_addr = inet_addr(linfo["lip"].c_str());
    }

    localaddr.sin_family    = AF_INET;
    localaddr.sin_port      = htons( atoi(linfo["lport"].c_str()));
    sock_                   = socket(AF_INET, SOCK_DGRAM, 0);

    if(sock_ < 0)
    {
        printf(" create udp socket errno %d:%s\n",errno, strerror(errno));
        return false;
    }

    /* bind address and port to socket */
    if(bind(sock_, (struct sockaddr *)&localaddr, sizeof(localaddr)) == -1)
    {
        printf("bind udp errno %d:%s\n",errno, strerror(errno));
        return false;
    }

    int val = (O_NONBLOCK | fcntl(sock_, F_GETFL));
    if( 0 != fcntl(sock_, F_SETFL, val))
    {
        printf("set udp non block errno %d:%s\n",errno, strerror(errno));
        return false;
    }

    int bufsize = 1024 * 1024;
    if( 0 != setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (char *)&bufsize, sizeof(int)))
    {
        printf("set udp SO_SNDBUF errno %d:%s\n",errno, strerror(errno));
        return false;
    }
    memset(&rmoteaddr_, 0, sizeof(rmoteaddr_));
    string ip = linfo["rip"];
    int port = (atoi(linfo["rport"].c_str()));
    rmoteaddr_.sin_addr.s_addr = inet_addr(ip.c_str());
    rmoteaddr_.sin_family       = AF_INET;
    rmoteaddr_.sin_port         = htons( port);

    if (linfo.end() != linfo.find("level")) {
        set_max_level((LogLevel)atoi(linfo["level"].c_str()));
    }

    if (linfo.end() != linfo.find("enable_usec")) {
        set_usec((utils::LogLevel)(atoi(linfo["enable_usec"].c_str())));
    }

    open();
    return true;
}


#if WIN32

#define DATE_START  0
char Log::level_str_[][64] = {
    "2008-11-07 09:35:00 FATAL ",
    "2008-11-07 09:35:00 ERROR ",
    "2008-11-07 09:35:00 WARN  ",
    "2008-11-07 09:35:00 INFO  ",
    "2008-11-07 09:35:00 DEBUG ",
    "2008-11-07 09:35:00 TRACE ",
};

char Log::level_str_usec_[][64] = {
    "\033[1;31m2008-11-07 09:35:00.000000 FATAL ",
    "\033[1;33m2008-11-07 09:35:00.000000 ERROR ",
    "\033[1;35m2008-11-07 09:35:00.000000 WARN  ",
    "\033[1;32m2008-11-07 09:35:00.000000 INFO  ",
    "\033[0;00m2008-11-07 09:35:00.000000 DEBUG ",
    "\033[0;00m2008-11-07 09:35:00.000000 TRACE ",
};

#else

#define DATE_START  7
char NLog::level_str_[][64] = {
    "\033[1;31m2008-11-07 09:35:00 FATAL ",
    "\033[1;33m2008-11-07 09:35:00 ERROR ",
    "\033[1;35m2008-11-07 09:35:00 WARN  ",
    "\033[1;32m2008-11-07 09:35:00 INFO  ",
    "\033[0;00m2008-11-07 09:35:00 DEBUG ",
    "\033[0;00m2008-11-07 09:35:00 TRACE ",
};

char NLog::level_str_usec_[][64] = {
    "\033[1;31m2008-11-07 09:35:00.000000 FATAL ",
    "\033[1;33m2008-11-07 09:35:00.000000 ERROR ",
    "\033[1;35m2008-11-07 09:35:00.000000 WARN  ",
    "\033[1;32m2008-11-07 09:35:00.000000 INFO  ",
    "\033[0;00m2008-11-07 09:35:00.000000 DEBUG ",
    "\033[0;00m2008-11-07 09:35:00.000000 TRACE ",
};

#endif

#define TIME_START  (DATE_START + 11)

NLog::NLog()
{
    sock_ = -1;
    max_level_ = L_INFO;
    enable_usec = false;
    enable_pack_print = true;
}

NLog::~NLog()
{
    close(sock_);
}

int NLog::set_max_level(LogLevel level)
{
    if (level < L_LEVEL_MAX) {
        max_level_ = level;
        return 0;
    }
    return 1;
}

int NLog::set_pack_print(bool in_enable_pack_print)
{
    enable_pack_print = in_enable_pack_print;
    return 0;
}

int NLog::set_usec(bool in_enable_usec)
{
    //printf("in_enable_usec:%d set.\n", in_enable_usec);
    enable_usec = in_enable_usec;
    return 1;
}

int NLog::open()
{
    time_t t;
    time(&t);
    struct tm lt = *localtime(&t);

    char name[32];
    // 填写日志记录中的日期，在一天之内就不用填写了
    strftime(name, 12, "%Y-%m-%d", &lt);
    for (int i = 0; i < L_LEVEL_MAX; i++) {
        memcpy(level_str_[i] + DATE_START, name, 10);
    }

    for (int i = 0; i < L_LEVEL_MAX; i++) {
        memcpy(level_str_usec_[i] + DATE_START, name, 10);
    }

    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    mid_night_ = mktime(&lt);

    return 0;
}

int NLog::log(LogLevel level, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vlog(level, fmt, ap); // not safe
    va_end(ap);
    return ret;
}

int NLog::log_fatal(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vlog(L_FATAL, fmt, ap);
    va_end(ap);
    return ret;
}

int NLog::log_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vlog(L_ERROR, fmt, ap);
    va_end(ap);
    return ret;
}

int NLog::log_warn(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vlog(L_WARN, fmt, ap);
    va_end(ap);
    return ret;
}


bool  NLog::strformatreplace(char * srcstr, char * desstr)
{
    if (NULL == srcstr || NULL == desstr)
    {
        return false;
    }

    if (strlen(srcstr) >= strlen(desstr))
    {
        return false;
    }
    unsigned int j = 0;
    desstr[j++] = srcstr[0];
    for(unsigned int i=1; i<strlen(srcstr); i++)
    {
        if(srcstr[i-1] == '%' && (srcstr[i] == 's' || srcstr[i] == 'S'))
        {
            if(j+5 >= strlen(desstr))
            {
                return false;
            }
            desstr[j++] = '.';
            desstr[j++] = '5';
            desstr[j++] = '1';
            desstr[j++] = '2';
            desstr[j++] = 's';
        }
        else
        {
            if(j >= strlen(desstr))
            {
                return false;
            }
            desstr[j++] = srcstr[i];
        }
    }
    if(j >= strlen(desstr))
        return false;

    desstr[j++] = '\0';

    return true;
}

int NLog::log_info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vlog(L_INFO, fmt, ap);
    va_end(ap);
    return ret;
}

int NLog::log_trace(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vlog(L_TRACE, fmt, ap);
    va_end(ap);
    return ret;
}

int NLog::log_debug(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vlog(L_DEBUG, fmt, ap);
    va_end(ap);
    return ret;
}

int NLog::vlog(int level, const char * fmt, va_list ap)
{
    if (level > max_level_ || -1 == sock_)
        return -1;
    char buf[MAX_LOG_BUF_SIZE+1024];
    bzero(buf,sizeof(buf));
    int buf_pos = 0;
    struct tm tm_now;
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv, &tz);
    time_t now = tv.tv_sec;

    int t_diff = (int)(now - mid_night_);
    if (t_diff > 24 * 60 * 60) {
        open();
        t_diff -= 24 * 60 * 60;
    }

    localtime_r(&now, &tm_now);
    if(enable_usec)
    {
        sprintf(((char*)level_str_usec_[level]+TIME_START), "%02d:%02d:%02d.%06ld",
                tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, (long) tv.tv_usec);
        level_str_usec_[level][TIME_START+15] = ' ';
        int n = snprintf(buf,sizeof(level_str_usec_[level]),"%s",level_str_usec_[level]);
        if( n>=0 ) buf_pos += n;
    }
    else
    {
        sprintf(((char*)level_str_[level]+TIME_START), "%02d:%02d:%02d",
                tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        level_str_[level][TIME_START+8] = ' ';
        int n = snprintf(buf,sizeof(level_str_[level]),"%s",level_str_[level]);
        if( n>=0 ) buf_pos += n;
    }

    char strformat[128]="";
    if (strformatreplace((char *) fmt, strformat))
    {
        int n = vsnprintf(buf+buf_pos,MAX_LOG_BUF_SIZE-buf_pos, strformat, ap);
        if( n>=0 ) buf_pos += n;
    }
    else
    {
        int n = vsnprintf(buf+buf_pos,MAX_LOG_BUF_SIZE-buf_pos, fmt, ap);
        if( n>=0 ) buf_pos += n;
    }

    if (fmt[strlen(fmt) - 1] != '\n')
    {
        buf[buf_pos] = '\n';
        //int n = snprintf(buf+buf_pos,2,"%c",'\n');
        buf_pos ++;
    }

    sendto(sock_, (const void *)buf, buf_pos, MSG_NOSIGNAL,(const struct sockaddr *)&rmoteaddr_,sizeof(struct sockaddr));

    return 0;
}


static const char chex[] = "0123456789ABCDEF";

int NLog::log_hex_prefix(
        unsigned char * prefix,
        unsigned char * data,
        size_t len,
        LogLevel level)
{
    log(level, "%s", prefix);
    return log_hex(data, len, level);
}

int NLog::log_hex(
        unsigned char * data,
        size_t len,
        LogLevel level)
{
    size_t i, j, k, l;

    if(level > max_level_ ||NULL == data|| -1 == sock_)
    {
        return -1;
    }
    if( len > 1024 ) len = 1024;
    //DON'T disable hex_print when level is  l_info, l_warn....
    if (!enable_pack_print && level > L_INFO)
        return -1;
    if( len > MAX_LOG_BUF_SIZE ) len = MAX_LOG_BUF_SIZE;
    char buf[MAX_LOG_BUF_SIZE];
    int buf_pos = 0;

    char msg_str[128] = {0};

    msg_str[0] = '[';
    msg_str[5] = '0';
    msg_str[6] = ']';
    msg_str[59] = ' ';
    msg_str[60] = '|';
    msg_str[77] = '|';
    msg_str[78] = 0;
    k = 6;
    for (j = 0; j < 16; j++)
    {
        if ((j & 0x03) == 0)
        {
            msg_str[++k] = ' ';
        }
        k += 3;
        msg_str[k] = ' ';
    }
    for (i = 0; i < len / 16; i++)
    {
        msg_str[1] = chex[i >> 12];
        msg_str[2] = chex[(i >> 8)&0x0F];
        msg_str[3] = chex[(i >>4)&0x0F];
        msg_str[4] = chex[i &0x0F];
        k = 7;
        l = i * 16;
        memcpy(msg_str + 61, data + l, 16);
        for (j = 0; j < 16; j++)
        {
            if ((j & 0x03) == 0)
            {
                k++;
            }
            msg_str[k++] = chex[data[l] >> 4];
            msg_str[k++] = chex[data[l++] & 0x0F];
            k++;
            if (!isgraph(msg_str[61 + j]))
                msg_str[61 + j]= '.';
        }
        msg_str[127] = 0;
        int n = snprintf(buf+buf_pos,MAX_LOG_BUF_SIZE-buf_pos,"# %s\n", msg_str);
        buf_pos += n;
    }

    msg_str[1] = chex[i >> 12];
    msg_str[2] = chex[(i >> 8)&0x0F];
    msg_str[3] = chex[(i >>4)&0x0F];
    msg_str[4] = chex[i &0x0F];

    k = 7;
    l = i * 16;
    memcpy(msg_str + 61, data + l, len % 16);
    for (j = 0; j < len % 16; j++)
    {
        if ((j & 0x03) == 0)
        {
            k++;
        }
        msg_str[k++] = chex[data[l] >> 4];
        msg_str[k++] = chex[data[l++] & 0x0F];
        k++;
        if (!isgraph(msg_str[61 + j]))
            msg_str[61 + j]= '.';
    }
    for (; j < 16; j++)
    {
        if ((j & 0x03) == 0)
        {
            k++;
        }
        msg_str[k++] = ' ';
        msg_str[k++] = ' ';
        k++;
        msg_str[61 + j]= ' ';
    }
    msg_str[127] = 0;
    int n = snprintf(buf+buf_pos,MAX_LOG_BUF_SIZE-buf_pos,"# %s\n", msg_str);
    buf_pos += n;
    sendto(sock_, (const void *)buf, buf_pos, MSG_NOSIGNAL,(const struct sockaddr *)&rmoteaddr_,sizeof(struct sockaddr));
    return 0;
}

NLog NLog::global_log;

} // end namespace utils


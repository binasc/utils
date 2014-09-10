#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.hpp"

namespace utils {

void daemon(const char *path)
{
    int fd;
    pid_t pid;
    struct rlimit limit;

    if((pid = fork()) < 0) {
        exit(EXIT_FAILURE);
    }
    else if (pid > 0) {
        exit(0);
    }

    /* 创建新的session和process group，成为其leader，并脱离控制终端 */
    setsid();

    /* 
     * 忽略终端IO信号和停止信号等
     */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP,  SIG_IGN);  /* 发给与控制终端相连的进程，表示与终端的连接断开 */
    signal(SIGINT,  SIG_IGN);  /* 来自终端的^c和delete */
    signal(SIGQUIT, SIG_IGN);  /* 来自终端的^/ */
    signal(SIGPIPE, SIG_IGN);  /* 向没有读进程的管道写错误 */
    signal(SIGTTOU, SIG_IGN);  /* 后台向终端写 */
    signal(SIGTTIN, SIG_IGN);  /* 后台从终端读 */

    if((pid = fork()) < 0) {
        exit(EXIT_FAILURE);
    }
    else if (pid > 0) { 
        exit(0);
    }

    /*
     * 工作目录更改为根目录。这是为了保证我们的进程不使用任何目录。否则我们的守护进程将一直
     * 占用某个目录，这可能会造成超级用户不能卸载一个文件系统。 
     */
    if(NULL == path) {
        chdir("/");
    }
    else {
        chdir(path);
    }

    /*
     * 将文件方式创建屏蔽字设置为"0"。这是因为由继承得来的文件创建方式屏蔽字可能会禁止某些许可权。
     * 例如我们的守护进程需要创建一组可读可写的文件，而此守护进程从父进程那里继承来的文件创建方式
     * 屏蔽字却有可能屏蔽掉了这两种许可权，则新创建的一组文件其读或写操作就不能生效。因此要将文件
     * 方式创建屏蔽字设置为"0"。
     */ 
    umask(0);

    //关闭打开的文档描述符，或重定向标准输入、标准输出和标准错误输出的文档描述符。
    //进程从创建他的父进程那里继承了打开的文档描述符。假如不关闭，将会浪费系统资源，
    //引起无法预料的错误。
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        exit(EXIT_FAILURE);
    }

    for (fd = limit.rlim_cur; fd > 0; fd--) {
        close(fd);
    }

    signal(SIGCHLD, SIG_IGN); 
}

int lock_wait(const char *fname)
{
    int fd, rc;;
    struct flock lock;
    char tmp[24];

    fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return -1;
    }

    sprintf(tmp, "%d", getpid());
    write(fd, tmp, strlen(tmp));

    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    lock.l_type = F_WRLCK;

    int error = -1;

    do{
        rc = fcntl(fd, F_SETLKW, &lock);
    } while(-1 == error && EINTR == errno);

    return 0;
} 

void partner(const char *lockname, char *argv[])
{
    int rc;
    pid_t pid;

    rc = lock_wait(lockname);
    if(0 == rc){
        if ((pid = fork()) == 0) {
            execv(argv[0], argv);
        }
        sleep(1);
    }
}

} // namespace utils


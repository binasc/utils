#ifndef __UTILS_HPP__
#define __UTILS_HPP__

namespace utils {

void daemon(const char *path = NULL);

// 启动搭档进程，argv里面参数必须带全路径
void partner(const char *lockname, char *argv[]);

} // namespace utils

#endif


#include <stdio.h>

//! @namespace utils
//! @brief utils命名空间
//!
//! Utils是一个基础工具库, 提供服务器开发常用的工具类.
namespace utils 
{
	namespace T
	{
		void daemon(const char * path = NULL);
		
		int lock_wait(const char * fname);
		
		void partner(const char * lockname, char* argv[]); // 启动搭档进程， argv 里面参数必须带全路径
	}
}


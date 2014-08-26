//! @file string_util.h
//! @brief 字符串工具类


#ifndef _STRING_UTIL_H_
#define _STRING_UTIL_H_


#include <string>
#include <vector>
#include <sstream>

using namespace std;


namespace utils {


//! 字符串工具类
class StringUtil
{
public:
	//! 转换为大写
	static string upper(string str);

	//! 转换为大写
	static void upper(char* str);

	//! 转换为小写
	static string lower(string str);

	//! 转换为小写
	static void lower(char *str);

	//! 去掉左侧的skip中的字符
	static string ltrim(string str, string skip = " \t");

	//! 去掉左侧的skip中的字符
	static void ltrim(char *str, const char *skip = " \t");

	//! 去掉右侧的skip中的字符
	static string rtrim(string str, string skip = " \t");

	//! 去掉右侧的skip中的字符
	static void rtrim(char *str, const char *skip = " \t");

	//! 去掉两侧的skip中的字符
	static string trim(string str, string skip = " \t");

	//! 去掉两侧的skip中的字符
	static void trim(char *str, const char *skip = " \t");

	static vector<std::string> split(const string src, const string sep);

	template <typename R>
    static  string toString(R t)
    {
            stringstream s;
            s<<t;
            return s.str();
    }
};


} // namepsace utils


#endif // _STRING_UTIL_H_

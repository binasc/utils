#include <string.h>
#include <stdlib.h>

#include "string_util.hpp"


namespace utils {


// class StringUtil
string StringUtil::upper(string str)
{
	string result = "";

	for (string::const_iterator c = str.begin(); c != str.end(); ++c)
	{
		result += toupper(*c);
	}

	return result;
}

void StringUtil::upper(char* str)
{
	int i = 0;
	while(str[i] != '\0' )
	{
		str[i] = toupper(str[i]);
		i++;
	}
}

string StringUtil::lower(string str)
{
	string result = "";

	for (string::const_iterator c = str.begin(); c != str.end(); ++c)
	{
		result += tolower(*c);
	}

	return result;
}

void StringUtil::lower(char *str)
{
	int i = 0;
	while(str[i] != '\0')
	{
		str[i] = tolower(str[i]);
		i++;
	}
}

string StringUtil::ltrim(string str, string skip)
{
	string::size_type pos;
	for (pos = 0; pos < str.length(); pos++)
	{
		if (string::npos == skip.find(str[pos]))
			break;
	}
	return str.substr(pos);
}

void StringUtil::ltrim(char *str, const char *skip)
{
	char s[2];
	s[1] = 0;

	size_t i;
	for (i = 0; i < strlen(str); i++)
	{
		s[0] = str[i];
		if (NULL == strstr(skip, s))
		{
			break;
		}
	}

	int j = 0;
	for (size_t p = i; p < strlen(str) + 1; p++)
	{
		str[j++] = str[p];
	}
}

string StringUtil::rtrim(string str, string skip)
{
	string::size_type pos;
	for (pos = str.length() - 1; pos >= 0; pos--)
	{
		if (string::npos == skip.find(str[pos]))
			break;
	}
	return str.substr(0, pos + 1);
}

void StringUtil::rtrim(char *str, const char *skip)
{
	char s[2];
	s[1] = 0;

	for (int i = (int)strlen(str); i >= 0; i--)
	{
		s[0] = str[i];
		if (NULL == strstr(skip, s))
		{
			break;
		}
		else
		{
			str[i] = 0;
		}
	}
}

string StringUtil::trim(string str, string skip)
{
	return rtrim(ltrim(str, skip), skip);
}

void StringUtil::trim(char *str, const char *skip)
{
	rtrim(str, skip);
	ltrim(str, skip);
}

vector<string> StringUtil::split(const string src, const string sep)
{
    vector<string> r;
    string s;
    for (string::const_iterator i = src.begin(); i != src.end(); i++)
    {
        if (sep.find(*i) != string::npos)
        {
            //if (s.length()) 
			r.push_back(s);
            s = "";
        }
        else
        {
            s += *i;
        }
    }
    if (s.length()) r.push_back(s);
    return r;
}

} // namepsace utils


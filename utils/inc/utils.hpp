#include <stdio.h>

//! @namespace utils
//! @brief utils�����ռ�
//!
//! Utils��һ���������߿�, �ṩ�������������õĹ�����.
namespace utils 
{
	namespace T
	{
		void daemon(const char * path = NULL);
		
		int lock_wait(const char * fname);
		
		void partner(const char * lockname, char* argv[]); // ��������̣� argv ������������ȫ·��
	}
}


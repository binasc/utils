#include "utils.hpp"
#include <stdio.h>
#include <netinet/in.h>
#include <signal.h>
#include <fcntl.h>
#include <iostream>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
namespace utils 
{
	namespace T
	{

		void daemon(const char * path )
		{
			pid_t iPID;
			
			if((iPID = fork()) < 0) /* -1�Ǵ���ʧ�ܵ���� */
			{
				//cout << "fork1 failed!" << endl;
				exit(0);
			}
			else if(iPID > 0){       /* ����0�ǽ��ӽ��̵�PID���ظ������� */
				exit(0);
			  }
			/* �����µ�session��process group����Ϊ��leader������������ն� */
			setsid();
			
			/* 
			 * signal�ĵڶ���������SIG_IGNʱ����ʾ��һ��������ָ���źŽ�������
			 * �����ն�IO�źź�ֹͣ�źŵ�
			 */
			signal(SIGINT,  SIG_IGN);  /* �����ն˵�ctrl+c��delete */
			signal(SIGHUP,  SIG_IGN);  /* ����������ն������Ľ��̣���ʾ���ն˵����ӶϿ� */
			signal(SIGQUIT, SIG_IGN);  /* �����ն˵�ctrl+/ */
			signal(SIGPIPE, SIG_IGN);  /* ��û�ж����̵Ĺܵ�д���� */
			signal(SIGTTOU, SIG_IGN);  /* ��̨���ն�д */
			signal(SIGTTIN, SIG_IGN);  /* ��̨���ն˶� */
			
			//signal(SIGTERM, SIG_IGN);  /* ��kill����������ȱʡ�ź� */
		
			if((iPID = fork()) < 0) 
			{
				//cout << "fork2 failed!" << endl;
				exit(0);
			}
			else if(iPID > 0){ 
				exit(0);
			}
		   /*
			* ����Ŀ¼����Ϊ��Ŀ¼������Ϊ�˱�֤���ǵĽ��̲�ʹ���κ�Ŀ¼���������ǵ��ػ����̽�һֱ
			* ռ��ĳ��Ŀ¼������ܻ���ɳ����û�����ж��һ���ļ�ϵͳ�� 
			*/
			if( NULL == path ) 
			{
				chdir("/");
			}
			else
			{
				chdir(path);
			}
			
			//�رմ򿪵��ĵ������������ض����׼���롢��׼����ͱ�׼����������ĵ���������
			//���̴Ӵ������ĸ���������̳��˴򿪵��ĵ������������粻�رգ������˷�ϵͳ��Դ��
			//�����޷�Ԥ�ϵĴ���getdtablesize()����ĳ���������ܴ򿪵������ĵ�����
			
			for (int fd=0,fdtablesize=getdtablesize();fd < fdtablesize;fd++)
			{
					close(fd);
				}
			
			
			/*
			 * ���ļ���ʽ��������������Ϊ"0"��������Ϊ�ɼ̳е������ļ�������ʽ�����ֿ��ܻ��ֹĳЩ���Ȩ��
			 * �������ǵ��ػ�������Ҫ����һ��ɶ���д���ļ��������ػ����̴Ӹ���������̳������ļ�������ʽ
			 * ������ȴ�п������ε������������Ȩ�����´�����һ���ļ������д�����Ͳ�����Ч�����Ҫ���ļ�
			 * ��ʽ��������������Ϊ"0"��
			 */ 
			umask(0);
			signal(SIGCHLD, SIG_IGN); 
			
		}
		
		int lock_wait(const char * fname)
		{
			//cout<<"--lock----[fname:]"<<fname<<endl;
			int fd = open(fname, O_RDWR | O_CREAT, 0666);
			
			if( fd < 0 ){
				//perror("open");
				return -1;
			}
			
			struct flock lock;
			lock.l_whence = SEEK_SET;
			lock.l_start = 0;
			lock.l_len = 0;
			lock.l_type = F_WRLCK;
		
		  int error = -1;
		  
		  do{
			
			error = fcntl(fd, F_SETLKW, &lock);  	
			
		  }while(-1==error && EINTR == errno);
		  
			return 0;
			
		} 

		void partner(const char * lockname, char* argv[])  // argv ��������������
		{
			int ret = lock_wait(lockname);
			if(0 == ret ){
				//printf("lock ok\n");
			  int pid = fork();
			  if(0==pid){
				//printf("[child] %s %s\n",arg[0],arg[1]);
				execv(argv[0], argv);
			  }
			  sleep(1);
			}	
		}
	
	}
}

	

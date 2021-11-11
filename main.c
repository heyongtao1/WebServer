#include<iostream>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/wait.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<assert.h>
#include<sys/epoll.h>
#include"threadpool.h"
#include"http_conn.h"
#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define PORT 8888
using namespace std;

extern int addfd(int epfd, int fd, bool flag);
extern int removefd(int epollfd,int fd);
 
void show_error(int connfd,const char* info)
{

	printf("%s",info);
	send(connfd,info,strlen(info),0);
	close(connfd);
}

void addsig(int sig,void(handler)(int),bool restart = true)
{
	struct sigaction sa;
	memset(&sa,'\0',sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
	{
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig,&sa,NULL) != -1);
}

int main(int argc, char *argv[])
{
	/*忽略管道信号*/
	addsig(SIGPIPE,SIG_IGN);
	/*创建线程池*/
    threadpool<http_conn>* pool = NULL;
	try
	{
		pool = new threadpool<http_conn>;
	}
    catch( ... )
	{
		return 1;
	}
	/* 预先创建客户任务对象 */
    http_conn* users = new http_conn[MAX_FD];
    assert(users);
	int user_count = 0;
	
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htons(INADDR_ANY);
 
    int listenfd = socket(AF_INET,SOCK_STREAM,0);
    assert(listenfd >= 0);
	struct linger tmp = {1,0};
	setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
 
    int ret = 0;
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);
 
    ret = listen(listenfd,5);
    assert(ret >= 0);
 
    int epfd;
    epoll_event events[MAX_EVENT_NUMBER];
    epfd = epoll_create(5);
    assert(epfd != -1);
    addfd(epfd, listenfd, false);//listen不能注册EPOLLONESHOT事件，否则只能处理一个客户连接
	
	/*全局epfd*/
	http_conn::m_epollfd = epfd;
    while(true)
    {
        int number = epoll_wait(epfd, events, MAX_EVENT_NUMBER, -1);
        if( (number < 0) && (errno != EINTR) )
        {
            printf("my epoll is failure!\n");
            break;
        }
        for(int i=0; i<number; i++)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd)//有新用户连接
            {
                struct sockaddr_in client_address;
                socklen_t client_addresslength = sizeof(client_address);
                int client_fd = accept(listenfd,(struct sockaddr*)&client_address, &client_addresslength);
                if(client_fd < 0)
                {
                    printf("errno is %d\n",errno);
                    continue;
                }
                /*如果连接用户超过了预定于的用户总数，则抛出异常*/
                if(http_conn::m_user_count > MAX_FD)
                {
                    show_error(client_fd, "Internal sever busy");
                    continue;
                }
                //初始化客户连接
                cout << "client_fd:" << client_fd << "**** connected success !\n";
                users[client_fd].init(client_fd,client_address);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                /*出现异常则关闭客户端连接*/
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN)//可以读取
            {
                cout << "have data can read"<<endl;
                if(users[sockfd].read())
                {
                    cout << "read success"<<endl;
                    /*读取成功则添加任务队列*/
                    pool->addjob(users+sockfd);
                }
                else{
					cout << "read fail"<<endl;
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT)//可写入
            {
                if(!users[sockfd].write())
                {
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epfd);
    close(listenfd);
    delete[] users;
    delete pool;
    return 0;
    
}
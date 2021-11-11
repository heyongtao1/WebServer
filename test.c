#include<iostream>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<assert.h>
#include<sys/epoll.h>
#include<string.h>

static const char* request = "GET http://localhost/sum.html HTTP/1.1\r\nConnection: Keep-alive\r\n\r\nxxxxxxxxxx";

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
 
void addfd(int epfd, int fd, bool flag)
{
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
    if(flag)
    {
        ev.events = ev.events | EPOLLONESHOT;
    }
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd);
}

bool write_nbytes(int sockfd,const char* buffer,int len)
{
	int bytes_write = 0;
	printf("write out %d bytes to socket %d\n",len,sockfd);
	while(1){
		bytes_write = send(sockfd,buffer,len,0);
		if(bytes_write == -1){
			return false;
		}
		if(bytes_write == 0){
			return false;
		}
		len -= bytes_write;
		buffer = buffer + bytes_write;
		if(len <= 0){
			return true;
		}
	}
}

bool read_once(int sockfd,char* buffer,int len)
{
	int bytes_read = 0;
	memset(buffer,'\0',len);
	bytes_read = recv(sockfd,buffer,len,0);
	if(bytes_read == -1 || bytes_read == 0){
		return false;
	}
	printf("read %d bytes form sockfd %d with connect buffer : %s\n",bytes_read,sockfd,buffer);
	return true;
}

void start_connected(int epollfd,int num,const char* ip,int port)
{
	int ret = 0;
	struct sockaddr_in address;
	bzero(&address,sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET,ip,&address.sin_addr);
	address.sin_port = htons(port);
	
	for(int i=0;i<num;i++){
		usleep(5000);
		int sockfd = socket(PF_INET,SOCK_STREAM,0);
		printf("create 1 socket fd = %d\n",sockfd);
		if(sockfd < 0){
			continue;
		}
		if(connect(sockfd,(struct sockaddr*)&address,sizeof(address)) == 0){
			printf("bulid connection %d\n",i);
			addfd(epollfd,sockfd,false);
		}
	}
}

void close_connected(int epollfd,int sockfd)
{
	epoll_ctl(epollfd,EPOLL_CTL_DEL,sockfd,0);
	close(sockfd);
}

int main(int argc,char* argv[])
{
	assert(argc == 4);
	int epoll_fd = epoll_create(100);
	start_connected(epoll_fd,atoi(argv[3]),argv[1],atoi(argv[2]));
	epoll_event events[10000];
	char buffer[2048];
	while(1){
		int fdnums = epoll_wait(epoll_fd,events,10000,2000);
		for(int i=0;i<fdnums;i++){
			int sockfd = events[i].data.fd;
			if(events[i].events & EPOLLIN){
				if(!read_once(sockfd,buffer,2048)){
					close_connected(epoll_fd,sockfd);
				}
				struct epoll_event event;
				event.events = EPOLLOUT | EPOLLET | EPOLLERR;
				event.data.fd = sockfd;
				epoll_ctl(epoll_fd,EPOLL_CTL_MOD,sockfd,&event);
			}
			else if(events[i].events & EPOLLOUT){
				if(!write_nbytes(sockfd,request,2048)){
					close_connected(epoll_fd,sockfd);
				}
				struct epoll_event event;
				event.events = EPOLLIN | EPOLLET | EPOLLERR;
				event.data.fd = sockfd;
				epoll_ctl(epoll_fd,EPOLL_CTL_MOD,sockfd,&event);
			}
			else if(events[i].events & EPOLLERR){
				close_connected(epoll_fd,sockfd);
			}	
		}
	}
}
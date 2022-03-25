#include<stdio.h>
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include<fcntl.h>
#include<ctype.h>
#include<errno.h>

#define MAX_EVENTS      4096*8
#define BUFLEN          512
#define DEFAULT_PORT    8080
#define TIMEOUT         60
#define LISTEN_NUM      1024*10

struct myevent
{
    int fd;
    int events;
    void* arg;
    void* (*call_back)(void* arg);          //回调函数
    int status;                             //是否在epoll中，0不在，1在
    char buf[BUFLEN + 1];
    int len;
    long last_active;                       //记录最后一次加入红黑树时间，用以判断是否需要过一段时间删除
};

extern int g_efd;


//初始化监听套接字
void init_listen_socket(int efd, int port);
//事件配置
void event_set(struct myevent* my_ev, int fd, void* (*call_back)(void*), void* arg);
//增加或修改epoll节点
void event_addOrMod(int efd, int events, struct myevent* my_ev);
//删除epoll节点
void event_del(int efd, struct myevent* my_ev);
//删除无事件发生的节点
void* event_check(void* arg);
//事件处理
void* acceptconn(void* arg);
void* recvdata(void* arg);
void* senddata(void* arg);

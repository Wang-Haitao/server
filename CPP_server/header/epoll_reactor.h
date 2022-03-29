/*

*/
#pragma once
#include<sys/epoll.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<fcntl.h>
#include<ctype.h>
#include<errno.h>
#include<exception>
#include<iostream>
#include<atomic>
#include<thread>
#include<signal.h>
#include<cstring>
#include"../header/threadpool.h"

#define MAX_CONN        10000
#define MAX_EVENTS      2048
#define TIMEOUT         60
#define BUFLEN          512
#define LISTEN_NUM      1024
#define SERVANT_NUM     2

//声明类
template<typename T>
class epoll_reactor_main;
template<typename T>
class epoll_reactor_servant;


//epoll事件结构
template<typename T>
struct myevent
{
    int fd;
    int events;
    T* user;
    epoll_reactor_servant<T>* reactor;      //事件关联的从机reactor
    int status;                             //是否在epoll中，0不在，1在
    long  last_active;                      //记录最后一次加入红黑树时间，用以判断是否需要过一段时间删除
};

//主reactor
template<typename T>
class epoll_reactor_main
{
public:
    epoll_reactor_main(int port, threadpool<T>* pool);
    ~epoll_reactor_main();
    //执行
    void run();

private:
    int m_lfd;                              //监听描述符
    int m_next_servant;                     //接受连接后分发给哪一个从机
    epoll_reactor_servant<T>* m_servant;    //从机集合
};


//从reactor
template<typename T>
class epoll_reactor_servant
{
public:
    //总连接数
    static std::atomic_int conn_num;
    epoll_reactor_servant();
    ~epoll_reactor_servant();
    //从reactor工作线程函数，负责网络IO和检测长时间未连接的socket
    static void run(epoll_reactor_servant<T>* thi);
    //从主机接收连接请求
    bool accept_conn(int cfd, T* user);
    //初始化事件属性
    void event_set(myevent<T>* my_ev, int fd);
    //添加结点
    void event_add(int events, myevent<T>* my_ev);
    //删除结点
    void event_del(myevent<T>* my_ev);
    void event_set_add(myevent<T>* my_ev, int fd, int events)
    {
        event_set(my_ev, fd);
        event_add(events, my_ev);
    }

    //更改成员
    //更改关联线程池
    void set_pool(threadpool<T>* new_pool) { m_pool = new_pool; }

private:
    bool shutdown;                          //关闭标志，标识以结束线程
    int m_efd;                              //从reactor的epoll根节点
    std::thread m_thr;                      //执行线程
    myevent<T>* m_events;                   //epoll事件集
    threadpool<T>* m_pool;                  //关联线程池，即任务放置到哪个线程池里
};


//----------------------------------主机实现-------------------------------//

template<typename T>
epoll_reactor_main<T>::epoll_reactor_main(int port, threadpool<T>* pool) :m_next_servant(0)
{
    m_lfd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    //端口复用
    int reuse = 1;
    int ret = setsockopt(m_lfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (ret == -1) throw std::exception();

    ret = bind(m_lfd, (sockaddr*) &servaddr, sizeof(servaddr));
    if (ret == -1) throw std::exception();
    ret = listen(m_lfd, LISTEN_NUM);
    if (ret == -1) throw std::exception();

    m_servant = new epoll_reactor_servant<T>[SERVANT_NUM];
    for (int i = 0;i < SERVANT_NUM;++i)
        m_servant[i].set_pool(pool);
}

template<typename T>
epoll_reactor_main<T>::~epoll_reactor_main()
{
    delete[] m_servant;
    close(m_lfd);
}

//主机执行
template<typename T>
void epoll_reactor_main<T>::run()
{
    sockaddr_in clieaddr;
    socklen_t clielen = sizeof(clieaddr);

    // T* user = new T[10000];
    T user[MAX_CONN];

    while (1)
    {
        int cfd = accept(m_lfd, (sockaddr*) &clieaddr, &clielen);

        if (cfd < 0)
        {
            perror("cfd:");
            continue;
        }
        if (epoll_reactor_servant<T>::conn_num == MAX_EVENTS * SERVANT_NUM)
        {
            std::cout << "max connet limit" << std::endl;
            close(cfd);
            continue;
        }

        bool flag = false;
        while (!flag)
            flag = m_servant[(m_next_servant++) % SERVANT_NUM].accept_conn(cfd, &user[cfd]);
        // std::cout << "connect sucessfully" << std::endl;
    }
}

//总连接数
template<typename T>
std::atomic_int epoll_reactor_servant<T>::conn_num(0);


//----------------------------------从机实现-------------------------------//


template<typename T>
epoll_reactor_servant<T>::epoll_reactor_servant() :shutdown(false), m_thr(run, this), m_pool(nullptr)
{
    m_efd = epoll_create(MAX_EVENTS);
    m_events = new myevent<T>[MAX_EVENTS];
    for (int i = 0;i < MAX_EVENTS;++i)
        m_events[i].status = 0;
}

template<typename T>
epoll_reactor_servant<T>::~epoll_reactor_servant()
{
    shutdown = true;
    m_thr.join();
    delete[] m_events;
}

//从主机接收连接请求
template<typename T>
bool epoll_reactor_servant<T>::accept_conn(int cfd, T* user)
{
    //查看是否有空闲位置
    int i = 0;
    for (i = 0;i < MAX_EVENTS;++i)
        if (m_events[i].status == 0) break;
    if (i == MAX_EVENTS) return false;
    epoll_reactor_servant::conn_num++;

    //设置非阻塞
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    m_events[i].user = user;
    user->set_ev(&m_events[i]);
    event_set_add(&m_events[i], cfd, EPOLLIN);
    return true;
}

//设置事件属性
template<typename T>
void epoll_reactor_servant<T>::event_set(myevent<T>* my_ev, int fd)
{
    my_ev->fd = fd;
    my_ev->reactor = this;
    my_ev->events = 0;
    my_ev->status = 0;
    my_ev->last_active = time(NULL);
}

//添加结点
template<typename T>
void epoll_reactor_servant<T>::event_add(int events, myevent<T>* my_ev)
{
    epoll_event epv = { 0,{0} };
    int op;
    epv.data.ptr = my_ev;
    events = events | EPOLLET | EPOLLRDHUP;
    epv.events = my_ev->events = events;

    if (my_ev->status == 1) op = EPOLL_CTL_MOD;
    else
    {
        op = EPOLL_CTL_ADD;
        my_ev->status = 1;
    }

    epoll_ctl(m_efd, op, my_ev->fd, &epv);
}

//删除结点
template<typename T>
void epoll_reactor_servant<T>::event_del(myevent<T>* my_ev)
{
    if (my_ev->status != 1) return;
    my_ev->status = 0;
    epoll_ctl(m_efd, EPOLL_CTL_DEL, my_ev->fd, NULL);
}


//从reactor工作线程函数，负责网络IO和检测长时间未连接的socket
template<typename T>
void epoll_reactor_servant<T>::run(epoll_reactor_servant<T>* thi)
{
    epoll_event events[MAX_EVENTS];


    while (thi->shutdown == false)
    {
        //检测长时间无事件的连接
        long now = time(NULL);
        for (int i = 0;i < MAX_EVENTS;i++)
        {
            if (thi->m_events[i].status != 1) continue;

            long duration = now - thi->m_events[i].last_active;
            if (duration >= TIMEOUT)
            {
                thi->m_events[i].user->close_conn();
                thi->event_del(&thi->m_events[i]);
            }
        }
        //获取事件
        int nfd = epoll_wait(thi->m_efd, events, MAX_EVENTS, TIMEOUT);
        for (int i = 0;i < nfd;i++)
        {
            myevent<T>* my_ev = (myevent<T>*) events[i].data.ptr;
            if ((events[i].events & EPOLLIN) && (my_ev->events & EPOLLIN))
            {
                if (my_ev->user->recvdata())
                    thi->m_pool->add_task(my_ev->user);
            }
            else if ((events[i].events & EPOLLOUT) && (my_ev->events & EPOLLOUT))
            {
                my_ev->user->senddata();
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP))
                my_ev->user->close_conn();

        }
    }

}


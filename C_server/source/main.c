#include"../header/server_epoll_reactor.h"
#include"../header/thread_pool.h"

int main(int argc, const char** argv)
{
    int port = DEFAULT_PORT;
    if (argc == 2) port = atoi(argv[1]);
    g_efd = epoll_create(MAX_EVENTS + 1);

    threadpool_t* thp = threadpool_create(5, 1000, 5000);
    printf("threadpool inited\n");

    init_listen_socket(g_efd, port);
    struct epoll_event events[MAX_EVENTS + 1];

    //添加常驻任务，删除长时间无事件发生的节点
    threadpool_add(thp, event_check, NULL);

    int  i;
    while (1)
    {
        int nfd = epoll_wait(g_efd, events, MAX_EVENTS + 1, -1);
        if (nfd < 0)
        {
            perror("epoll_wait error");
            break;
        }
        for (i = 0;i < nfd;++i)
        {
            struct myevent* my_ev = (struct myevent*) events[i].data.ptr;
            if ((events[i].events & EPOLLIN) && (my_ev->events & EPOLLIN))
                threadpool_add(thp, my_ev->call_back, my_ev->arg);
            if ((events[i].events & EPOLLOUT) && (my_ev->events & EPOLLOUT))
                threadpool_add(thp, my_ev->call_back, my_ev->arg);
        }

    }
    threadpool_destroy(thp);
    return 0;
}
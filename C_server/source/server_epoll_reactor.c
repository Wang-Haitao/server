#include"../header/server_epoll_reactor.h"

int g_efd;
struct myevent g_events[MAX_EVENTS + 1];

void init_listen_socket(int efd, int port)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int flag = fcntl(lfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(lfd, F_SETFL, flag);

    event_set(&g_events[MAX_EVENTS], lfd, acceptconn, &g_events[MAX_EVENTS]);
    event_addOrMod(efd, EPOLLIN, &g_events[MAX_EVENTS]);

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    bind(lfd, (struct sockaddr*) &servaddr, sizeof(servaddr));
    listen(lfd, LISTEN_NUM);

    return;

}

void event_set(struct myevent* my_ev, int fd, void* (*call_back)(void*), void* arg)
{
    my_ev->fd = fd;
    my_ev->call_back = call_back;
    my_ev->events = 0;
    my_ev->arg = arg;
    my_ev->status = 0;
    my_ev->last_active = time(NULL);
}

void event_addOrMod(int efd, int events, struct myevent* my_ev)
{
    struct epoll_event epv = { 0,{0} };
    int op;
    epv.data.ptr = my_ev;
    events |= EPOLLET;
    epv.events = my_ev->events = events;

    if (my_ev->status == 1) op = EPOLL_CTL_MOD;
    else
    {
        op = EPOLL_CTL_ADD;
        my_ev->status = 1;
    }

    epoll_ctl(efd, op, my_ev->fd, &epv);
}

void event_del(int efd, struct myevent* my_ev)
{
    if (my_ev->status != 1) return;
    my_ev->status = 0;
    epoll_ctl(efd, EPOLL_CTL_DEL, my_ev->fd, NULL);
}

void* event_check(void* arg)
{
    while (1)
    {
        int checkpos = 0;
        long now = time(NULL);
        for (int i = 0;i < 100;i++, checkpos++)
        {
            if (checkpos == MAX_EVENTS) checkpos = 0;
            if (g_events[checkpos].status != 1) continue;

            long duration = now - g_events[checkpos].last_active;
            if (duration >= TIMEOUT)
            {
                close(g_events[checkpos].fd);
                event_del(g_efd, &g_events[checkpos]);
            }
        }
    }
}

void* acceptconn(void* arg)
{
    struct myevent* my_ev = (struct myevent*) arg;
    struct sockaddr_in clieaddr;
    socklen_t clie_len = sizeof(clie_len);
    int cfd, i;
    char str[INET_ADDRSTRLEN];

    while ((cfd = accept(my_ev->fd, (struct sockaddr*) &clieaddr, &clie_len)) > 0)
    {
        printf("received from %s at port %d\n"
            , inet_ntop(AF_INET, &clieaddr.sin_addr.s_addr, str, sizeof(str))
            , ntohs(clieaddr.sin_port));

        for (i = 0;i < MAX_EVENTS;i++)
            if (g_events[i].status == 0) break;
        if (i == MAX_EVENTS)
        {
            printf("%d : max connet limit\n", MAX_EVENTS);
            return NULL;
        }
        int flag = fcntl(cfd, F_GETFL);
        flag |= O_NONBLOCK;
        fcntl(cfd, F_SETFL, flag);

        event_set(&g_events[i], cfd, recvdata, &g_events[i]);
        event_addOrMod(g_efd, EPOLLIN, &g_events[i]);
    }
    if (cfd == -1 && errno != EAGAIN)
    {
        perror("accept error");
    }
}

void* recvdata(void* arg)
{
    struct myevent* my_ev = (struct myevent*) arg;
    int len;
    event_del(g_efd, my_ev);
    //非阻塞读
    int read_index = 0;
    while ((len = recv(my_ev->fd, my_ev->buf + read_index, BUFLEN, 0)) > 0)
    {
        // for (int i = 0;i <= len;i++) my_ev->buf[i + read_index] = toupper(my_ev->buf[i + read_index]);
        read_index += len;
    }
    if (len == -1)
    {
        //数据读完了,准备发送
        if (errno == EAGAIN)
        {
            my_ev->len = read_index;
            // write(STDOUT_FILENO, my_ev->buf, len);
            event_set(my_ev, my_ev->fd, senddata, my_ev);
            event_addOrMod(g_efd, EPOLLOUT, my_ev);
        }
        else
        {
            close(my_ev->fd);
            perror("recvdata error");
        }
    }
    else if (len == 0)
    {
        close(my_ev->fd);
        printf("recv:closed\n");
    }
}

void* senddata(void* arg)
{
    struct myevent* my_ev = (struct myevent*) arg;
    int len;
    event_del(g_efd, my_ev);

    //非阻塞写
    int write_index = 0;
    while (1)
    {
        if ((len = send(my_ev->fd, my_ev->buf + write_index, my_ev->len, 0)) > 0)
        {
            write_index += len;
            if (write_index == my_ev->len)
            {
                //测试webbench
                // close(my_ev->fd);
                // break;
                event_set(my_ev, my_ev->fd, recvdata, my_ev);
                event_addOrMod(g_efd, EPOLLIN, my_ev);
                break;
            }
        }
        else if (len == -1)
        {
            if (errno == EAGAIN)
            {
                break;
            }
            else
            {
                perror("senddata error");
                close(my_ev->fd);
                break;
            }
        }
        else if (len == 0)
        {
            close(my_ev->fd);
            printf("send:closed\n");
            break;
        }
    }

}

#pragma once
#include"epoll_reactor.h"

class test
{
private:
    char* m_read_buf;
    char* m_send_buf;
    int m_send_len;
    myevent<test>* m_ev;
public:

    test() :m_ev(nullptr), m_send_len(0)
    {
        m_read_buf = new char[256];
        m_send_buf = new char[256];
    }
    ~test()
    {
        delete[] m_read_buf;
        delete[] m_send_buf;
    }
    //设置m_ev
    void set_ev(myevent<test>* new_ev) { m_ev = new_ev; }
    //连接关闭
    void close_conn() { close(m_ev->fd); }

    //业务处理
    void process();

    //数据接收
    bool recvdata();

    //数据发送
    void senddata();

};

bool test::recvdata()
{
    int len;

    m_ev->reactor->event_del(m_ev);

    //非阻塞读
    int read_index = 0;
    while ((len = recv(m_ev->fd, m_read_buf + read_index, BUFLEN, 0)) > 0)
        read_index += len;

    if (len == -1)
    {
        //数据读完了,准备处理
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            m_send_len = read_index;
            // write(STDOUT_FILENO, m_read_buf, read_index);
            return true;
        }
        else
        {
            close_conn();
            return false;
        }
    }
    else if (len == 0)
    {
        close_conn();
        return false;
    }
    return false;
}

void test::senddata()
{
    int len;
    m_ev->reactor->event_del(m_ev);

    //非阻塞写
    int write_index = 0;
    while (1)
    {
        if ((len = send(m_ev->fd, m_send_buf + write_index, m_send_len, 0)) > 0)
        {
            write_index += len;
            if (write_index == m_send_len)
            {
                // m_ev->reactor->event_set(m_ev, m_ev->fd);
                // m_ev->reactor->event_add(EPOLLIN, m_ev);
                close_conn();
                break;
            }
        }
        else if (len == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // m_ev->reactor->event_set(m_ev, m_ev->fd);
                // m_ev->reactor->event_add(EPOLLIN, m_ev);
                close_conn();
                return;
            }

            else
            {
                close_conn();
                return;
            }
        }
        else if (len == 0)
        {
            close_conn();
            return;
        }
    }

}

void test::process()
{

    strncpy(m_send_buf, m_read_buf, m_send_len);
    m_ev->reactor->event_set(m_ev, m_ev->fd);
    m_ev->reactor->event_add(EPOLLOUT, m_ev);

}
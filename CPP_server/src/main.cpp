
#include"../header/http_conn.h"
// #include"../header/test.h"

int main(int argc, char** argv)
{
    threadpool<http_conn>* pool = new threadpool<http_conn>();
    if (argc != 2)
    {
        std::cout << "error: ./server port_number" << std::endl;
        return 0;
    }
    epoll_reactor_main<http_conn>* reactor;
    try
    {
        reactor = new epoll_reactor_main<http_conn>(atoi(argv[1]), pool);
    }
    catch (...)
    {
        std::cout << "error: reactor" << std::endl;
        return 0;
    }

    reactor->run();
    delete pool;
    delete reactor;
    return 0;
}
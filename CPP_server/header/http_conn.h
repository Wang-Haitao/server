
#pragma once
#include"epoll_reactor.h"
#include<sys/stat.h>
#include<unistd.h>
#include<sys/mman.h>
#include<cstdarg>
#include<sys/uio.h>
static const int MAX_FILENAME_LEN = 128;    // 文件名最大长度
static const int READ_BUFFER_SIZE = 512;    // 读缓冲大小
static const int WRITE_BUFFER_SIZE = 512;   // 写缓冲大小


// HTTP请求方法，这里只支持GET
enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT };

/*
    解析客户端请求时，主状态机的状态
    CHECK_STATE_REQUESTLINE:当前正在分析请求行
    CHECK_STATE_HEADER:当前正在分析头部字段
    CHECK_STATE_CONTENT:当前正在解析请求体
*/
enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

/*
    从状态机的三种可能状态，即行的读取状态，分别表示
    1.读取到一个完整的行 2.行出错 3.行数据不完整
*/
enum LINE_STATE { LINE_OK = 0, LINE_BAD, LINE_INCOM };

/*
    服务器处理HTTP请求的可能结果，报文解析的结果
    INCOM_REQUEST       :   请求不完整，需要继续读取客户数据
    GET_REQUEST         :   表示获得了一个完整的客户请求
    BAD_REQUEST         :   表示客户请求语法错误
    NO_RESOURCE         :   表示服务器没有资源
    FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
    FILE_REQUEST        :   文件请求,获取文件成功
    INTERNAL_ERROR      :   表示服务器内部错误
    CLOSED_CONNECTION   :   表示客户端已经关闭连接了
*/
enum HTTP_CODE { INCOM_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };




class http_conn
{
public:

    http_conn();
    ~http_conn();

    void process();                 //业务处理
    bool recvdata();                //数据接收
    void senddata();                //数据发送
    void close_conn();              //连接关闭
    //设置m_ev
    void set_ev(myevent<http_conn>* new_ev) { m_ev = new_ev; }

private:

    //初始化连接信息
    void init();

    HTTP_CODE process_read();                               //解析http请求
    //process_read调用以解析HTTP请求。
    HTTP_CODE parse_request_line(char* text);               //解析请求行
    HTTP_CODE parse_head(char* text);                       //解析头部
    HTTP_CODE parse_context(char* text);                    //解析内容
    LINE_STATE parse_line();                                //解析一行
    char* get_line() { return m_read_buf + m_start_line; }
    HTTP_CODE do_request();


    bool process_write(HTTP_CODE ret);                       //填充HTTP应答
    // process_write调用以填充HTTP应答。
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_keeplive();
    bool add_blank_line();

private:
    char* m_read_buf;
    int m_read_index;
    int m_check_index;              //当前分析的位置
    int m_start_line;               //正在解析的行位置
    CHECK_STATE m_check_state;      //主状态机目前位置

    METHOD m_method;
    char* m_url;                    //客户请求的目标文件的文件名
    char* m_version;                //HTTP协议版本号，我们仅支持HTTP1.1
    char* m_host;                   //主机名
    int m_content_length;           //HTTP请求的消息总长度
    bool m_keeplive;                //HTTP请求是否要求保持连接

    char* m_send_buf;
    int m_send_len;
    int m_send_index;               //写缓冲区中待发送的字节数
    char* m_file_address;           //客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;        //目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    iovec m_iv[2];                  //我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;                 //分块读数量

    char m_file[MAX_FILENAME_LEN];  //客户请求的目标文件的完整路径

    myevent<http_conn>* m_ev;
};


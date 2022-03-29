#include"../header/http_conn.h"

// HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 资源根目录
const char* root_resource = "/home/wht/webserver/CPP_server/resource";


http_conn::http_conn() :m_ev(nullptr)
{
    m_read_buf = new char[READ_BUFFER_SIZE];
    m_send_buf = new char[WRITE_BUFFER_SIZE];
    init();
}

http_conn::~http_conn()
{
    close_conn();
    delete[] m_read_buf;
    delete[] m_send_buf;
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始状态为检查请求行
    m_keeplive = false;       // 默认不保持链接  Connection : keep-alive保持连接
    m_method = GET;         // 默认请求方式为GET
    m_url = nullptr;
    m_version = nullptr;
    m_content_length = 0;
    m_host = nullptr;
    m_start_line = 0;
    m_check_index = 0;
    m_read_index = 0;
    m_send_index = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_send_buf, READ_BUFFER_SIZE);
    bzero(m_file, MAX_FILENAME_LEN);
}
//连接关闭
void http_conn::close_conn()
{
    // std::cout << "connect close" << std::endl;
    epoll_reactor_servant<http_conn>::conn_num--;
    init();
    close(m_ev->fd);
}

//数据接收
bool http_conn::recvdata()
{
    if (m_read_index >= READ_BUFFER_SIZE) return false;
    int len;
    m_ev->reactor->event_del(m_ev);
    //非阻塞读
    while ((len = recv(m_ev->fd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0)) > 0)
        m_read_index += len;

    if (len == -1)
    {
        //数据读完了,准备处理
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
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
    return true;
}

//数据发送
void http_conn::senddata()
{
    int len;
    int no_context_index = m_iv[0].iov_len;
    int write_index = 0;
    m_ev->reactor->event_del(m_ev);
    //非阻塞写
    while (1)
    {
        len = writev(m_ev->fd, m_iv, m_iv_count);
        if (len > 0)
        {
            write_index += len;
            if (write_index == m_send_len)
            {
                unmap();
                m_ev->reactor->event_set_add(m_ev, m_ev->fd, EPOLLIN);
                if (m_keeplive) init();
                return;
            }
            else if (write_index >= m_iv[0].iov_len)
            {
                m_iv[1].iov_base = m_file_address + (write_index - no_context_index);
                m_iv[0].iov_len = 0;
                m_iv[1].iov_len = m_send_index - write_index;
            }
            else
            {
                m_iv[0].iov_base = m_send_buf + write_index;
                m_iv[0].iov_len = m_iv[0].iov_len - len;
            }
        }
        else if (len == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                unmap();
                m_ev->reactor->event_set_add(m_ev, m_ev->fd, EPOLLIN);
                if (m_keeplive) init();
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

//业务处理
void http_conn::process()
{
    //解析请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == INCOM_REQUEST)
    {
        m_ev->reactor->event_set_add(m_ev, m_ev->fd, EPOLLIN);
        return;
    }
    //响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) close_conn();
    m_ev->reactor->event_set_add(m_ev, m_ev->fd, EPOLLOUT);
}

//解析http请求
HTTP_CODE http_conn::process_read()
{
    LINE_STATE line_state = LINE_OK;
    HTTP_CODE ret = INCOM_REQUEST;
    char* text = nullptr;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_state = LINE_OK))
        || (line_state = parse_line()) == LINE_OK)
    {
        text = get_line();
        m_start_line = m_check_index;

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE: {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER: {
            ret = parse_head(text);
            if (ret == BAD_REQUEST) return BAD_REQUEST;
            else if (ret == GET_REQUEST) return do_request();
            break;
        }
        case CHECK_STATE_CONTENT: {
            ret = parse_context(text);
            if (ret == GET_REQUEST) return do_request();
            line_state = LINE_INCOM;
            break;
        }
        default: return INTERNAL_ERROR;
        }
    }
    return INCOM_REQUEST;
}

//解析请求行,method URL version
HTTP_CODE http_conn::parse_request_line(char* text)
{
    //获取方法
    m_url = strpbrk(text, " ");
    if (!m_url) return BAD_REQUEST;
    *m_url++ = '\0'; char* method = text;

    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else
        return BAD_REQUEST;
    //获取url
    m_version = strpbrk(m_url, " ");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //获取版本
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    m_check_state = CHECK_STATE_HEADER;

    return INCOM_REQUEST;
}

//解析头部
HTTP_CODE http_conn::parse_head(char* text)
{
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0')
    {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return INCOM_REQUEST;
        }
        // 否则已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " ");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_keeplive = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " ");
        m_content_length = atoi(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " ");
        m_host = text;
    }
    else
    {
        std::cout << " unknow header " << text << std::endl;
    }
    return INCOM_REQUEST;
}

//解析内容
//目前没实现，只是判断是否进缓冲区
HTTP_CODE http_conn::parse_context(char* text)
{
    if (m_read_index >= (m_content_length + m_check_index))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return INCOM_REQUEST;
}

//解析一行
LINE_STATE http_conn::parse_line()
{
    char temp;
    for (;m_check_index < m_read_index;++m_check_index)
    {
        temp = m_read_buf[m_check_index];
        if (temp == '\r')
        {
            if (m_check_index + 1 == m_read_index)
                return LINE_INCOM;
            else if (m_read_buf[m_check_index + 1] == '\n')
            {
                m_read_buf[m_check_index++] = '\0';
                m_read_buf[m_check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_check_index > 0) && (m_read_buf[m_check_index - 1] == '\r'))
            {
                m_read_buf[m_check_index - 1] = '\0';
                m_read_buf[m_check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_INCOM;
}

/*  当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
    如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
    映射到内存地址m_file处，获取文件成功
*/
HTTP_CODE http_conn::do_request()
{

    strcpy(m_file, root_resource);
    int len = strlen(root_resource);
    strncpy(m_file + len, m_url, MAX_FILENAME_LEN - len - 1);
    std::cout << m_file << std::endl;
    // 获取m_file文件的相关的状态信息
    if (stat(m_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char*) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ...)
{
    if (m_send_index >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_send_buf + m_send_index, WRITE_BUFFER_SIZE - 1 - m_send_index, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_send_index))
    {
        return false;
    }
    m_send_index += len;
    va_end(arg_list);
    return true;
}

//写响应状态行
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

//写头部
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_keeplive();
    add_blank_line();
    return true;
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_keeplive()
{
    return add_response("Connection: %s\r\n", (m_keeplive == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

//目前只有html文件
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}


// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    case BAD_REQUEST:
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    case NO_RESOURCE:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    case FILE_REQUEST:
        add_status_line(200, ok_200_title);
        add_headers(m_file_stat.st_size);
        m_iv[0].iov_base = m_send_buf;
        m_iv[0].iov_len = m_send_index;
        m_iv[1].iov_base = m_file_address;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_iv_count = 2;

        m_send_index += m_file_stat.st_size;

        return true;
    default:
        return false;
    }

    m_iv[0].iov_base = m_send_buf;
    m_iv[0].iov_len = m_send_index;
    m_iv_count = 1;
    return true;
}

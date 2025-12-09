#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>//定义了系统基本数据类型, ssize_t, size_t
#include <sys/uio.h> //定义了向量 I/O (Vector I/O) 操作相关的结构体和函数
#include <arpa/inet.h>//提供了互联网操作相关的定义，主要是 IP 地址转换和网络地址结构体
#include <stdlib.h> //标准通用工具库 (Standard Library)内存分配 (malloc/free)、类型转换 (atoi) 等基础函数。虽然 C++ 有 new/delete，但底层很多操作仍可能依赖此库
#include <errno.h> //定义了错误码宏, EAGAIN / EWOULDBLOCK：这是非阻塞 I/O 中最重要的错误码

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn{
public:
    HttpConn();
    ~HttpConn();

    //init: 初始化连接。因为服务器通常使用对象池复用 HttpConn 对象（避免频繁 new/delete），
    //所以当新连接到来时，直接调用 init 重置状态
    void init(int sockFd, const sockaddr_in& addr);

    //I/O 操作 (最底层)
    ssize_t read(int* saveErrno);
    ssize_t write(int* saveErrno);

    void Close();
    int GetFd() const;
    int GetPort() const;
    const char* GetIP() const;
    sockaddr_in GetAddr() const;

    //这是由工作线程（ThreadPool）调用的主逻辑函数
    bool process();

    int ToWriteBytes(){
        return iov_[0].iov_len + iov_[1].iov_len;
    }

    bool IsKeepAlive() const{
        return request_.IsKeepAlive();
    }
    
    // 是否开启 Epoll 的 ET (Edge Trigger) 模式
    static bool isET;
    // 网站根目录资源路径 (如 /home/web/resources)
    static const char* srcDir;
    // 原子整数：统计当前有多少个活跃连接
    static std::atomic<int> userCount;

private:
    int fd_;
    struct sockaddr_in addr_;

    bool isClose_;

    int iovCnt_;
    struct iovec iov_[2];

    // 读缓冲区：存储从 socket 读出来的原始数据
    Buffer readBuff_;
    // 写缓冲区：存储准备发给客户端的 HTTP 头部信息
    Buffer writeBuff_;

    // 解析器：解析 readBuff_ 中的数据
    HttpResponse response_;
    // 响应生成器：根据 request_ 的结果，准备文件映射和头部
    HttpRequest request_;
};

#endif //HTTP_CONN_H
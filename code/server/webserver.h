#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
//用于建立网络连接
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../log/log.h"
#include "../timer/heaptimer.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"
#include "../pool/sqlconnRAII.h"
#include "../http/httpconn.h"

class WebServer{
public:
    // 构造函数：初始化服务器的各种参数
    WebServer(
        int port, int trigmODE, int timeoutMs, bool OptLinger,  // 网络与超时配置
        int sqlPort, const char* sqlUser, const char* sqlPwd,   // 数据库配置
        const char* dbName, int connPoolNum, int threadNum,     // 资源池配置
        bool openLog, int logLevel, int logQueSize              // 日志配置
    );
    ~WebServer();// 析构函数：释放资源（关闭 socket，停止线程池等）
    void Start();// 【启动按钮】：调用后服务器开始无限循环运行
    
private:
    //创建监听 Socket
    bool InitSocket_();
    // 配置 Epoll 模式（ET 边缘触发 或 LT 水平触发）。
    void InitEventMode_(int trigmODE);
    //初始化一个 HttpConn 对象放入 users_ 哈希表，并设置定时器
    void AddClient_(int fd, sockaddr_in addr);

    //处理新连接
    void DealListen_();
    //主线程将 client 写缓冲区的数据发送给网卡。
    void DealWrite_(HttpConn* client);
    //主线程读取数据 -> 放入 client 的读缓冲区 -> 将任务扔给线程池。
    void DealRead_(HttpConn* client);

    //发送错误信息（如服务器繁忙）
    void SendError_(int fd, const char* info);
    //如果客人有动作，重置他的超时时间，防止被定时器踢掉。
    void ExtentTime_(HttpConn* client);
    //关闭连接，从 epoll 中移除，释放资源。
    void CloseConn_(HttpConn* client);

    //具体的读取逻辑
    void OnRead_(HttpConn* client);
    //具体的发送逻辑
    void OnWrite_(HttpConn* client);
    //解析 HTTP 请求 -> 生成 HTTP 响应。
    void OnProcess(HttpConn* client);

    static const int MAX_FD = 65536;

    static int SetFdNonblock(int fd);

    // 1. 基础配置
    int port_;// 端口号
    bool openLinger_;// 是否优雅关闭
    int timeoutMS_;  // 超时时间 (毫秒)，超过这个时间不发请求就会被断开
    bool isClose_;   // 服务器是否停止运行
    int listenFd_;   // 监听的文件描述符 (大门)
    char* srcDir_;   // 网站根目录路径 (HTML文件存放处)

    // 2. Epoll 事件配置
    uint32_t listenEvent_;  // 监听 socket 的事件模式 (通常是 EPOLLIN | EPOLLET)
    uint32_t connEvent_;    // 连接 socket 的事件模式

    // 3. 核心子系统 (使用智能指针 unique_ptr 管理生命周期)
    std::unique_ptr<HeapTimer> timer_;  // 定时器堆 (管理超时连接)
    std::unique_ptr<ThreadPool> threadpool_;    // 线程池 (处理计算密集型任务)
    std::unique_ptr<Epoller> epoller_;  // Epoll 对象 (IO 多路复用)
    // 4. 客户名单
    // key: 文件描述符 fd (int)
    // value: 具体的连接对象 (HttpConn)
    // 作用: 通过 fd 快速找到对应的客户数据
    std::unordered_map<int, HttpConn> users_;
};

#endif //WEBSERVER_H
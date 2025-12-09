#include "webserver.h"

using namespace std;

WebServer::WebServer(
    int port, int trigmODE, int timeoutMs, bool OptLinger,
    int sqlPort, const char* sqlUser, const char* sqlPwd,
    const char* dbName, int connPoolNum, int threadNum,
    bool openLog, int logLevel, int logQueSize
): port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMs), isClose_(false), 
timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
{
    srcDir_ = getcwd(nullptr, 256); // 获取当前工作目录的绝对路径, 当第一个参数传 nullptr 时，getcwd 函数内部会调用 malloc 在堆上分配内存来存储路径字符串。
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);// 拼接上资源文件夹名
    HttpConn::userCount = 0;    // 计数器归零
    HttpConn::srcDir = srcDir_; // 将路径共享给所有的 HttpConn 对象
    // 初始化数据库连接池 (单例模式)
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    // 设置 ET (边缘触发) 还是 LT (水平触发)
    InitEventMode_(trigmODE);
    // 尝试打开端口监听
    if(!InitSocket_()) {isClose_ = true;}

    if(openLog){
        // 初始化日志单例：设置级别、路径、后缀、队列大小
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) {LOG_ERROR("========== Server init error!==========");}
        else{
            // 打印启动成功的详细信息，方便运维排查
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s", (listenEvent_ & EPOLLET ? "ET": "LT"), (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer(){
    // 一个 WebServer 对象通常只监听一个端口,这个 Socket 是 WebServer 类直接持有的唯一一个“入口”资源
    // 当有一个用户连进来时，操作系统会返回一个新的 Socket，我们通常叫它 clientFd
    // 这些新生成的 clientFd 不是由 WebServer 直接作为一个个成员变量管理的，而是被放进了 users_ 哈希表中
    // 这个 users_ 表里可能存了 10,000 个 HttpConn 对象。每个对象内部管理着自己的 clientFd
    // Epoll (多路复用) 的作用就是：虽然你只有一副耳朵（一个线程），但 Epoll 帮你同时盯着这 10,000 个连接。
    // 谁有数据发过来，Epoll 就通知你去处理谁。
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::InitEventMode_(int trigmODE){
    listenEvent_ = EPOLLRDHUP; //监听 Socket, 默认属性：EPOLLRDHUP（检测对方是否挂断）
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP; //通信 Socket, 如果不加 EPOLLONESHOT：当大量数据到来时，Epoll 可能会多次触发。此时，线程 A 正在处理第一波数据，Epoll 又通知了第二波数据，线程 B 可能被唤醒去处理同一个 Socket。导致两个线程同时操作同一个 Socket，发生严重错误。
    switch(trigmODE)
    {
        case 0:
            break;
        case 1:
            connEvent_ |= EPOLLET;
            break;
        case 2:
            listenEvent_ |= EPOLLET;
            break;
        case 3:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        default:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start(){
    int timeMs = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_){LOG_INFO("========== Server start ==========");}
    while(!isClose_){
        // 1. 获取最近的超时时间
        // 如果开启了定时器，我们需要算出“离最近一个连接超时还有多久”
        // 比如最近一个连接将在 50ms 后超时，那 epoll_wait 最多只能等 50ms，
        // 醒来后好去处理那个超时连接。
        if(timeoutMS_ > 0){
            timeMs = timer_->GetNextTick();
        }
        // 2. 等待事件 (核心阻塞点)
        // 这一步会让出 CPU，直到有网络事件或超时
        int eventCnt = epoller_->Wait(timeMs);
        // 3. 处理所有发生的事件
        for(int i = 0; i < eventCnt; i++){
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            // A. 处理新连接 (Listen Socket 有动静)
            if(fd == listenFd_){
                DealListen_();
            }
            // B. 处理异常/挂断 (错误或对端关闭)
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }
            // C. 处理读事件 (客户端发数据来了)
            else if(events & EPOLLIN){
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }
            // D. 处理写事件 (缓冲区满了变空了，可以发数据了)
            else if(events & EPOLLOUT){
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char*info){
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

void WebServer::CloseConn_(HttpConn* client){
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}

void WebServer::AddClient_(int fd, sockaddr_in addr){
    assert(fd > 0);
    users_[fd].init(fd, addr);
    if(timeoutMS_ > 0){
        //std::bind 的作用就是“打包”： 它把 函数名 + 对象指针 + 参数 全部打包成一个看起来像 void() 的闭包对象。
        //&WebServer::CloseConn_：我要调用的函数。
        //this：在当前这个 WebServer 对象上调用。
        //&users_[fd]：参数是这个具体的连接对象。
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    //EPOLLOUT 的触发条件是：TCP 发送缓冲区（Send Buffer）有空位，可以写入数据
    //刚建立连接时，发送缓冲区肯定是空的, 如果你监听了 EPOLLOUT，Epoll 会立刻、马上通知你：“嘿！可以写数据了！”
    // 但是，你此时根本还没收到客户端的请求，你不知道要写什么（不知道回 200 还是 404）。
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

void WebServer::DealListen_(){
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do{
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if( fd <= 0){return;}
        else if(HttpConn::userCount >= MAX_FD){
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd,addr);
    } while(listenEvent_ & EPOLLET);
}

//Reactor 模式 的典型体现：主线程只负责“分发任务”，不负责“干活”。
void WebServer::DealRead_(HttpConn* client){
    assert(client);
    // 1. 续命：只要有读写动作，就重置超时时间，防止被踢
    ExtentTime_(client);
    // 2. 扔进线程池：将具体的 OnRead_ 函数绑定好参数，作为任务抛给线程池
    // 主线程立刻返回，继续去处理下一个 Epoll 事件
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

void WebServer::DealWrite_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

void WebServer::ExtentTime_(HttpConn* client){
    assert(client);
    if(timeoutMS_ > 0) {
        timer_->adjust(client->GetFd(), timeoutMS_);
    }
}

//业务逻辑回调 (OnRead_, OnProcess, OnWrite_)在线程池里跑的代码
void WebServer::OnRead_(HttpConn* client){
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client -> read(&readErrno);
    if(ret <= 0 && readErrno != EAGAIN){
        CloseConn_(client);
        return;
    }
    OnProcess(client);
}

void WebServer::OnProcess(HttpConn* client){
    // client->process() 会解析 HTTP 请求
    if(client->process()){
        // 成功生成响应 -> 修改监听事件为 EPOLLOUT
        // 下次 Epoll 就会通知“可以写了”，然后触发 OnWrite_
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    }else{
        // 请求不完整 (半包) -> 继续监听 EPOLLIN，等剩下的数据来
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn* client){
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret  = client->write(&writeErrno);
    if(client -> ToWriteBytes() == 0){
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* Create listenFd */
bool WebServer::InitSocket_(){
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024){
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family  = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = {0};
    if(openLinger_){
        /* 优雅关闭: 调用 close 时，如果缓冲区还有数据，内核会尝试等待 1 秒钟把数据发完再彻底关闭。*/
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0){
        LOG_ERROR("Create socket error!", port_);
        return false;
    }
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0){
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 防止服务器重启时，因为之前的连接处于 TIME_WAIT 状态而导致端口被占用无法启动。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

int WebServer::SetFdNonblock(int fd){
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}
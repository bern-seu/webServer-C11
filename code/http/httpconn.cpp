#include "httpconn.h"
using namespace std;

// 初始化静态成员变量
const char* HttpConn::srcDir;
// 原子计数器
std::atomic<int> HttpConn::userCount;
// 是否开启 ET (Edge Trigger) 模式
bool HttpConn::isET;

HttpConn::HttpConn(){
    fd_ = -1;
    addr_ = {0};
    isClose_ = true;
}

HttpConn::~HttpConn(){
    Close();
}

void HttpConn::init(int fd, const sockaddr_in& addr){
    assert(fd > 0);
    // 原子加1，无需加锁
    userCount++;
    addr_ = addr;
    fd_ = fd;
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
}

void HttpConn::Close(){
    // 解除内存映射，释放 mmap 占用的内存
    response_.UnmapFile();
    if(isClose_ == false){
        isClose_ = true;
        userCount--;
        close(fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const{
    return fd_;
}

struct sockaddr_in HttpConn::GetAddr() const{
    return addr_;
}
const char* HttpConn::GetIP() const{
    return inet_ntoa(addr_.sin_addr);
}
int HttpConn::GetPort() const{
    return addr_.sin_port;
}

//! 传大文件时可能存在内存耗尽的问题，解决方法：限制最大请求大小、临时文件 (Nginx）
ssize_t HttpConn::read(int* saveErrno){
    ssize_t len = -1;
    do{
        // 调用 Buffer 的 ReadFd 方法，实际执行 recv 系统调用
        len = readBuff_.ReadFd(fd_, saveErrno);
        if(len <= 0){
            break;  // 读完了（EAGAIN）或者出错了
        }
    } while(isET);  // 如果是 ET 模式，必须循环读直到缓冲区为空
    return len;
}
//发送数据（最复杂的指针运算）
//把 Buffer 里的头 和 mmap 里的文件 发送出去。
ssize_t HttpConn::write(int* saveErrno){
    ssize_t len = -1;
    do{
        len = writev(fd_, iov_, iovCnt_);
        if(len <= 0){
            *saveErrno = errno;
            break;
        }
        if(iov_[0].iov_len + iov_[1].iov_len == 0){break;}/* 传输结束 */
        else if(static_cast<size_t>(len) > iov_[0].iov_len){
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if(iov_[0].iov_len){
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else{
            iov_[0].iov_base = (uint8_t*) iov_[0].iov_base + len;
            iov_[0].iov_len -= len;
            writeBuff_.Retrieve(len);
        }
    } while(isET || ToWriteBytes() > 10240); // 如果是 ET 模式，必须一次性发完（或者发到缓冲区满返回 EAGAIN）
                                             // 如果剩余待发送的数据还很大（超过 10KB），那就继续在这个循环里发，尽量多发一点，减少系统调用的切换开销
    return len;
}

bool HttpConn::process(){
    // 1. 如果读缓冲区没数据，没法处理
    if(readBuff_.ReadableBytes() <= 0){
        return false;
    }
    // 1. 调用 parse
    bool isValid = request_.parse(readBuff_);
    // 【情况 1: 格式错误】 -> 返回 false (Bad Request)
    if (!isValid) {
        LOG_ERROR("Syntax Error");
        response_.Init(srcDir, request_.path(), false, 400);; // 准备 400 页面
    }
    // 到了这里，说明 isValid == true，数据目前是合法的
    // 接下来区分是“完事了”还是“还要等”
    else if(request_.state() == HttpRequest::FINISH){
        // 解析成功 (200 OK)
        LOG_DEBUG("%s", request_.path().c_str());
        // 初始化响应：设置路径，状态码200
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
    }else{
        // 【情况 3: 解析未完】 -> Incomplete
        // isValid 是 true，但 state 还没到 FINISH
        return false; // 告诉 WebServer：别急，继续监听 EPOLLIN，等下一波数据
    }

    // 3. 生成响应头，写入 writeBuff_
    response_.MakeResponse(writeBuff_);
    /* 响应头 */
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    /* 文件 */
    if(response_.FileLen() > 0 && response_.File()){
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    }
    LOG_DEBUG("filesize:%d, %d to %d", response_.FileLen(), iovCnt_, ToWriteBytes());
    request_.Init();
    return true;
}
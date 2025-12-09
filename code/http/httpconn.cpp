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
    request_.Init(); // 重置解析器状态
    // 1. 如果读缓冲区没数据，没法处理
    if(readBuff_.ReadableBytes() <= 0){
        return false;
    }
    // 2. 解析 HTTP 请求 (GET /index.html ...)
    else if(request_.parse(readBuff_)){
        // 解析成功 (200 OK)
        LOG_DEBUG("%s", request_.path().c_str());
        // 初始化响应：设置路径，状态码200
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200);
    }else{
        // 解析失败 (400 Bad Request)
        response_.Init(srcDir, request_.path(), false, 400);
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
    return true;
}
#ifndef LOG_H
#define LOG_H

#include <mutex>
#include <string>
#include <thread>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end // 处理可变参数（日志格式化）
#include <assert.h>
#include <sys/stat.h>         //mkdir
#include "blockqueue.h"
#include "../buffer/buffer.h"

class Log{
public:
    // 初始化日志：级别、路径、后缀、异步队列容量
    void init(int level, const char* path = "./log",const char* suffix = ".log", int maxQueueCapacity = 1024);
    // 单例模式：获取唯一日志实例
    static Log* Instance();
    // 异步日志的刷写线程入口
    static void FlushLogThread();

    // 写日志（支持可变参数格式化）
    void write(int level, const char* format,...);
    // 刷新缓冲区到文件
    void flush();
    // 获取/设置日志级别
    int GetLevel();
    void SetLevel(int level);
    // 检查日志是否开启
    bool IsOpen(){return isOpen_;}

private:
    // 私有构造/析构：禁止外部创建/销毁实例（单例）
    Log();
    // 拼接日志级别前缀（如[DEBUG]、[INFO]）
    void AppendLogLevelTitle_(int level);
    virtual ~Log();
    // 异步写日志的核心逻辑
    void AsyncWrite_();
private:
    // 常量定义：路径长度、文件名长度、单文件最大行数
    static const int LOG_PATH_LEN = 256;
    static const int LOG_NAME_LEN = 256;
    static const int MAX_LINES = 50000;

    // 日志存储路径（如"./log"）
    const char* path_;
    // 日志文件后缀（如".log"）
    const char* suffix_;

    // 单文件最大行数（可配置）
    int MAX_LINES_;
     // 当前文件已写入行数
    int lineCount_;
    // 当前日期（防止跨天写入同一文件）
    int toDay_;
    // 日志是否开启
    bool isOpen_;
    // 自定义缓冲区（减少频繁IO）
    Buffer buff_;
    // 当前日志级别（低于该级别不输出）
    int level_;
    // 是否开启异步模式
    bool isAsync_;

    // 日志文件指针
    FILE* fp_;
    // 阻塞队列：异步模式下暂存日志内容
    std::unique_ptr<BlockDeque<std::string>> deque_;
    // 异步写日志的线程
    std::unique_ptr<std::thread> writeThread_;
    std::mutex mtx_;
};
#define LOG_BASE(level, format, ...)\
    do{\
        Log* log = Log::Instance();\
        if(log->IsOpen() && log->GetLevel() <= level){\
            log->write(level,format,##__VA_ARGS__);\
            log->flush();\
        }\
    }while(0);

#define LOG_DEBUG(format,...)do{LOG_BASE(0,format,##__VA_ARGS__)} while(0);
#define LOG_INFO(format,...) do {LOG_BASE(1, format,##__VA_ARGS__)} while(0);
#define LOG_WARN(format,...) do {LOG_BASE(2,format,##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

#endif //LOG_H
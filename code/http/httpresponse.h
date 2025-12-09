#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <fcntl.h>      // 提供open()函数（打开文件）
#include <unistd.h>    // 提供close()/read()等系统调用
#include <sys/stat.h>   // 提供stat结构体/stat()函数（获取文件状态：大小、类型等）
#include <sys/mman.h>    // 提供mmap()/munmap()（内存映射文件，提升文件读取效率）

#include "../buffer/buffer.h"   // 自定义缓冲区类（用于拼接HTTP响应数据，减少IO次数）
#include "../log/log.h"         // 自定义日志类（记录响应处理中的错误/信息）

class HttpResponse{
public:
    // 构造函数：初始化成员变量（如code_=-1、isKeepAlive_=false等）
    HttpResponse(); 
    // 析构函数：通常会调用UnmapFile()释放内存映射，避免内存泄漏
    ~HttpResponse();

    //初始化响应对象核心参数
    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    //构建完整的 HTTP 响应（状态行 + 响应头 + 响应体），并写入自定义缓冲区buff
    void MakeResponse(Buffer& buff);
    //解除文件的内存映射（调用munmap()），释放mmFile_指向的内存。
    void UnmapFile();
    //返回内存映射后的文件指针（mmFile_），用于直接读取文件内容。
    char* File();
    //返回映射文件的长度（从mmFileStat_.st_size获取），const 表示不修改成员变量。
    size_t FileLen() const;
    //构建错误响应的响应体（如 404 页面内容），写入缓冲区。
    void ErrorContent(Buffer& buff, std::string message);
    //内联函数，返回当前响应状态码（code_）
    int Code() const {return code_;}


private:
    //构建 HTTP 响应的状态行（如HTTP/1.1 200 OK），写入缓冲区。
    void AddStateLine_(Buffer& buff);
    //构建 HTTP 响应的响应头（如Content-Type: text/html、Connection: keep-alive等），写入缓冲区。
    void AddHeader_(Buffer& buff);
    //构建 HTTP 响应的响应体（文件内容或错误页面内容），写入缓冲区。
    void AddContent_(Buffer& buff);

    //	根据状态码（如 404、500）定位错误页面的路径（如/404.html）。
    void ErrorHtml_();
    //根据文件后缀（如.html、.css）获取对应的 MIME 类型（如text/html）。
    std::string GetFileType_();

    //HTTP 响应状态码（200 = 成功、404 = 文件不存在、500 = 服务器内部错误等）
    int code_;
    //是否保持 TCP 长连接（决定响应头Connection的值是keep-alive还是close）。
    bool isKeepAlive_;

    //请求文件的完整路径
    std::string path_;
    //网站根目录（拼接path_得到完整文件路径的
    std::string srcDir_;

    //内存映射后的文件指针（通过mmap()将文件映射到内存，比 read () 更高效）
    char* mmFile_;
    //文件状态结构体（存储文件大小、是否为普通文件、修改时间等）。
    struct stat mmFileStat_;

    //文件后缀→MIME 类型映射（如.html→text/html; charset=utf-8、.jpg→image/jpeg）
    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    //状态码→状态描述映射（如 200→OK、404→Not Found、500→Internal Server Error）
    static const std::unordered_map<int, std::string> CODE_STATUE;
    //状态码→错误页面路径映射（如 404→/404.html、500→/500.html)
    static const std::unordered_map<int, std::string> CODE_PATH;
};

#endif //HTTP_RESPONSE_H
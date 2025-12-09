#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map> //存储 HTTP 头部、POST 参数、默认页面配置
#include <unordered_set>
#include <string>
#include <regex> //处理字符串和正则匹配（HTTP 解析常用）
#include <errno.h> //错误码处理
#include <mysql/mysql.h> 

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include "../pool/sqlconnRAII.h"

class HttpRequest{
public:
    enum PARSE_STATE{
        // 解析请求行（如GET /index.html HTTP/1.1）
        REQUEST_LINE,
        // 解析请求头部（如Host: localhost）
        HEADERS,
        // 解析请求主体（POST请求的参数）
        BODY,
        // 解析完成
        FINISH,
    };

    enum HTTP_CODE{
        // （错误：重复定义）请求不完整，需继续接收数据
        NO_REQUEST = 0,
        // 解析成功，获取到有效请求
        GET_REQUEST,
        // 请求格式错误
        BAD_REQUEST,
        // 权限拒绝
        FORBIDDENT_REQUEST,
        // 文件请求（如静态资源）
        FILE_REQUEST,
        // 服务器内部错误
        INTERNAL_ERROR,
        // 连接关闭
        CLOSED_CONNECTION,
    };

    // 构造时初始化
    HttpRequest() {Init();}
    // 默认析构
    ~HttpRequest() = default;
    // 初始化成员变量（重置解析状态、清空请求数据等）
    void Init();

    //从缓冲区buff中解析 HTTP 请求，是核心入口函数
    bool parse(Buffer& buff);
    //获取 / 修改请求路径
    std::string path() const;
    std::string& path();
    //获取请求方法
    std::string method() const;
    //获取 HTTP 版本
    std::string version() const;
    //获取 POST 请求的参数（支持string/char*键）
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    //判断是否为长连接
    bool IsKeepAlive() const;

private:
    //解析请求行（提取方法、路径、版本）
    bool ParseRequestLine_(const std::string& line);
    //解析请求头部（存入header_哈希表）
    void ParseHeader_(const std::string& line);
    //解析请求主体（POST 参数存入body_）
    void ParseBody_(const std::string& line);

    //处理请求路径（如补全默认页面/→/index.html）
    void ParsePath_();
    //解析 POST 数据（如application/x-www-form-urlencoded格式）
    void ParsePost_();
    //解析 URL 编码的 POST 参数（如username=abc&pwd=123）
    void ParseFromUrlencoded_();

    //静态函数，验证用户名密码（结合 MySQL 数据库）
    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);

    //当前解析状态（状态机的核心）
    PARSE_STATE state_;
    //存储请求的关键部分
    std::string method_, path_, version_, body_;
    //哈希表，存储请求头部,键值对，如Host: 127.0.0.1）
    std::unordered_map<std::string, std::string> header_;
    //哈希表，存储 POST 参数（键值对，如username: admin）
    std::unordered_map<std::string, std::string> post_;

    //无序集合，存储默认 HTML 页面（如/index.html//login.html）
    static const std::unordered_set<std::string> DEFAULT_HTML;
    //哈希表，映射页面路径到标识（如/login→1）
    static const std::unordered_map<std::string,int> DEFAULT_HTML_TAG;
    //静态函数，将十六进制字符（如A/3）转为十进制，用于解析 URL 编码
    static int ConverHex(char ch);
};

#endif //HTTP_REQUEST_H
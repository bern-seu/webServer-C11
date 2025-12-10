#include "httprequest.h"
using namespace std;

//存储无需后缀的简洁路径（如/login），用于补全.html后缀
const unordered_set<string> HttpRequest::DEFAULT_HTML{
    "/index", "/register", "/login",
    "/welcome", "/video", "/picture", };
//映射页面路径到业务标签（0 = 注册，1 = 登录），用于区分用户操作类型
const unordered_map<string,int> HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html",0}, {"/login.html",1},
};

//重置请求解析状态、清空成员变量，为新请求做准备；
void HttpRequest::Init(){
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}
//判断是否为 HTTP 长连接（检查Connection: keep-alive且 HTTP/1.1）
bool HttpRequest::IsKeepAlive() const{
    if(header_.count("Connection") == 1){
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

bool HttpRequest::parse(Buffer& buff){
    // HTTP协议的行分隔符（回车+换行）
    const char CRLF[] = "\r\n";
    if(buff.ReadableBytes() <= 0){// 缓冲区无数据，直接返回
        return true;
    }
    // 定义最大行长度，例如 8KB (通常足够放下 URL 和大部分 Header), 超过这个长度还没换行，肯定是恶意攻击或错误
    const size_t MAX_LINE_LEN = 8192;
    while(buff.ReadableBytes() && state_ != FINISH){
        // 特殊处理 BODY：不找 CRLF，而是看 Content-Length
        if (state_ == BODY) {
            ParseBody_(buff); // 直接把 Buffer 传进去，而不是传 string line
            // ParseBody_ 内部会判断数据够不够 Content-Length
            // 如果够了，就设置 state_ = FINISH
            // 如果不够，就 return，等待下一次数据
            break; // Body 处理通常是一次性的或者流式的，处理完一轮就跳出
        }
        //查找当前行的结束位置（CRLF）
        const char* lineEnd = search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF + 2);
        // 没找到 CRLF -> 说明行不完整 -> 退出等待更多数据
        if(lineEnd == buff.BeginWrite()) { 
            if (buff.ReadableBytes() > MAX_LINE_LEN) {
                LOG_ERROR("Line too long! Potential buffer overflow attack.");
                return false; // 直接判死刑：400 Bad Request
            }
            break; 
        }
        //提取当前行的字符串（从可读起始到CRLF前）
        std::string line(buff.Peek(),lineEnd);
        // 移动读指针（跳过当前行 + CRLF）
        buff.RetrieveUntil(lineEnd + 2);
        //按当前状态处理行数据
        switch(state_)
        {
            case REQUEST_LINE:
                if(!ParseRequestLine_(line)){// 解析请求行失败
                    return false;
                }
                ParsePath_();// 处理请求路径（如补全默认页面、转义特殊字符）
                break;
            case HEADERS:
                if (line.empty()) { 
                    state_ = BODY; 
                    // 优化：如果是 GET 或 Content-Length=0，直接完成
                    if(header_.count("Content-Length") == 0) { state_ = FINISH; }
                    break;
                }
                if(!ParseHeader_(line)) return false; // 【错误】Header 格式不对
                break;
            default:
                break;
        }
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

void HttpRequest::ParsePath_(){
    if(path_ == "/"){
        path_ = "/index.html";
    }
    else if(DEFAULT_HTML.count(path_)){// 直接查找是否存在，无需遍历
        path_ += ".html";
    }
}

bool HttpRequest::ParseRequestLine_(const string& line){
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line, subMatch,patten)){
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

bool HttpRequest::ParseHeader_(const string& line){
    // 推荐使用更严谨的正则
    regex patten("^([^:]+): ?(.*)$");
    smatch subMatch;
    if(regex_match(line,subMatch,patten)){
        header_[subMatch[1]] = subMatch[2];
        return true;
    }
    // 【关键】：匹配失败，进入这里
    else{
        LOG_ERROR("Header format error: %s", line.c_str());
        return false; // 返回 false
    }
}
// TODO:解析请求体，可能要分情况，如果是上传文件呢？
void HttpRequest::ParseBody_(Buffer& buff){
    // 1. 获取 Body 长度
    int contentLen = 0;
    if(header_.count("Content-Length")) {
        contentLen = stoi(header_["Content-Length"]);
    }

    // 2. 检查数据够不够
    if(contentLen > 0) {
        if(buff.ReadableBytes() >= contentLen) {
            // 从当前读指针开始，拷贝 contentLen 个字节到 body_
            body_ = buff.RetrieveToStr(contentLen);
            state_ = FINISH;
        }
        // else: 数据不够，什么都不做，函数结束，外层 parse 返回 true
        // 等待 Epoll 下次触发读取更多数据
    } else {
        // 没有 Content-Length，通常视为没有 Body
        state_ = FINISH;
    }
}

int HttpRequest::ConverHex(char ch){
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1; // 无效十六进制字符返回-1
}

void HttpRequest::ParsePost_(){
    //仅处理POST 请求且Content-Type 为表单格式（application/x-www-form-urlencoded）的情况，过滤其他类型的请求（如 GET、JSON 格式的 POST）
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded"){
        //调用ParseFromUrlencoded_()函数，将body_中的 URL 编码字符串（如username=admin&password=123）解析为键值对
        ParseFromUrlencoded_();
        if(DEFAULT_HTML_TAG.count(path_)){
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1){
                bool isLogin = (tag == 1);
                if(UserVerify(post_["username"],post_["password"],isLogin)){
                    path_ = "/welcome.html"; // 验证成功：重定向到欢迎页
                }else{
                    path_ = "/error.html"; // 验证失败：重定向到错误页
                }
            }
        }
    }
}

void HttpRequest::ParseFromUrlencoded_(){
    if(body_.size() == 0) { return; }

    string key, value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;

    for(; i < n; i++) {
        char ch = body_[i];
        switch (ch) {
        case '=':
            key = body_.substr(j, i - j);
            j = i + 1;
            break;
        case '+':
            body_[i] = ' ';
            break;
        case '%':
            num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
            body_[i + 2] = num % 10 + '0';
            body_[i + 1] = num / 10 + '0';
            i += 2;
            break;
        case '&':
            value = body_.substr(j, i - j);
            j = i + 1;
            post_[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if(post_.count(key) == 0 && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

bool HttpRequest::UserVerify(const string& name, const string &pwd, bool isLogin){
    if(name == "" || pwd == ""){return false;}
    LOG_INFO("Verify name:%s pwd:%s", name.c_str(),pwd.c_str());
    MYSQL* sql;
    SqlConnRAII(&sql, SqlConnPool::Instance());
    assert(sql);

    bool flag = false;// 最终验证结果标记
    unsigned int j = 0;
    char order[256] = {0};// 存储SQL语句的缓冲区
    MYSQL_FIELD *fields = nullptr;// 字段信息
    MYSQL_RES *res = nullptr;// 数据库查询结果集

    if(!isLogin){flag = true;}// 注册场景：先假设用户名可用（后续查询存在则改为false）
    /* 查询用户及密码 */
    snprintf(order,256,"SELECT username,password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);// 日志输出SQL语句（调试用）

    if(mysql_query(sql,order)){// 执行SQL查询
        mysql_free_result(res);// 释放结果集（避免内存泄漏）
        return false;// 查询失败直接返回false
    }
    res = mysql_store_result(sql);// 获取查询结果集（全部加载到内存）
    j = mysql_num_fields(res);// 获取结果集的字段数（未实际使用）
    fields = mysql_fetch_fields(res);// 获取字段信息（未实际使用）

    while(MYSQL_ROW row = mysql_fetch_row(res)){// 遍历结果集（最多1条，因LIMIT 1）
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);// 输出查询到的用户名和密码
        string password(row[1]);// 数据库中存储的密码
        /* 登录行为*/
        if(isLogin){
            if(pwd == password){flag = true;}// 密码匹配：验证成功
            else{
                flag = false;// 密码不匹配：验证失败
                LOG_DEBUG("pwd error!");
            }
        }else{
            // 注册行为：查到用户名存在 → 标记为false（用户名已被占用）
            flag = false;
            LOG_DEBUG("user used!");
        }
    }
    // 释放结果集资源
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true){
        LOG_DEBUG("register!");
        bzero(order, 256);// 清空SQL缓冲区
        // 构造插入SQL：新增用户记录
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) { // 执行插入
            LOG_DEBUG( "Insert error!");// 插入失败（如主键冲突）
            flag = false; 
        }
        flag = true;// 插入成功：注册成功
    }
    SqlConnPool::Instance()->FreeConn(sql);// 归还数据库连接到连接池
    LOG_DEBUG( "UserVerify success!!");// 日志标记验证流程结束
    return flag;// 返回最终验证结果
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}
std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

HttpRequest::PARSE_STATE HttpRequest::state() const {
    return state_;
}
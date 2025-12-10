#include "httpresponse.h"

using namespace std;

//根据文件的后缀名（如 .html, .jpg），决定 HTTP 响应头中的 Content-Type。
const unordered_map<string, string> HttpResponse::SUFFIX_TYPE = {
    {".html", "text/html"},
    {".xml", "text/xml"},
    {".xhtml", "application/xhtml+xml"},
    {".txt", "text/plain"},
    {".rtf", "application/rtf"},
    {".pdf", "application/pdf"},
    {".word", "application/msword"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".au", "audio/basic"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},
    {".avi", "video/x-msvideo"},
    {".gz", "application/x-gzip"},
    {".tar", "application/x-tar"},
    {".css", "text/css"},
    {".js", "text/javascript"},
};

//根据数字状态码（如 200, 404）获取对应的英文描述（如 "OK", "Not Found"）
const unordered_map<int,string> HttpResponse::CODE_STATUE = {
    { 200, "OK"},
    { 400, "Bad Request"},
    { 403, "Forbidden"},
    { 404, "Not Found"},
};

//当发生 400/403/404 错误时，服务器不返回原本请求的文件，而是自动重定向到这些预定义的 HTML 错误页面
const unordered_map<int,string> HttpResponse::CODE_PATH = {
    { 400, "/400.html"},
    { 403, "/403.html"},
    { 404, "/404.html"},
};
//初始化成员变量
HttpResponse::HttpResponse(){
    code_ = -1;
    path_ = srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
}
//确保释放内存映射资源，防止内存泄漏
HttpResponse::~HttpResponse(){
    UnmapFile();
}
//重置对象状态。因为服务器通常使用对象池或重复利用对象来处理多个请求，所以在处理新请求前必须清空旧数据（如 mmFile_ 指针、状态码等）
void HttpResponse::Init(const string& srcDir, string& path, bool isKeepAlive, int code){
    assert(srcDir != "");
    if(mmFile_) { UnmapFile(); }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr;
    mmFileStat_ = {0};
}
//这是生成响应的主入口函数
void HttpResponse::MakeResponse(Buffer& buff){
    /* 判断请求的资源文件 */
    
    //系统调用 stat 会读取磁盘上的文件元数据（大小、权限、类型等）并写入 mmFileStat_
    if (code_ < 400) { 
        if (stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
            code_ = 404; // 没找到或者是个目录
        }
        else if (!(mmFileStat_.st_mode & S_IROTH)) {
            code_ = 403; // 没权限读
        }
        else {
            code_ = 200; // 文件存在且可读，确认状态为 200
        }
    }
    //如果状态码是错误的（如 404），将 path_ 修改为对应的错误页面路径（如 /404.html）
    ErrorHtml_();
    //构建响应报文：依次调用以下三个函数向 Buffer 中写入数据
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

char* HttpResponse::File(){
    return mmFile_;
}

size_t HttpResponse::FileLen() const{
    return mmFileStat_.st_size;
}

void HttpResponse::ErrorHtml_(){
    if(CODE_PATH.count(code_) == 1){
        path_ = CODE_PATH.find(code_)->second;
        stat((srcDir_ + path_).data(), &mmFileStat_);
    }
}
//(添加状态行)HTTP/1.1 状态码 状态描述\r\n
void HttpResponse::AddStateLine_(Buffer& buff){
    string status;
    if(CODE_STATUE.count(code_) == 1){
        status = CODE_STATUE.find(code_)->second;
    }
    else{
        code_ = 400;
        status = CODE_STATUE.find(code_)->second;
    }
    buff.Append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");
}
//(添加响应头)
void HttpResponse::AddHeader_(Buffer& buff){
    buff.Append("Connection: ");
    if(isKeepAlive_){
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    }else{
        buff.Append("close\r\n");
    }
    buff.Append("Content-type: " + GetFileType_() + "\r\n");
}
//内存映射, 处理大文件传输的核心优化部分
void HttpResponse::AddContent_(Buffer& buff){
    //使用 open 打开目标文件
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);
    if(srcFd < 0){
        ErrorContent(buff,"File NotFound");
        return;
    }
    /* 将文件映射到内存提高文件的访问速度 
        MAP_PRIVATE 建立一个写入时拷贝的私有映射*/
    LOG_DEBUG("file path %s", (srcDir_ + path_).data());
    //mmap (Memory Map)。它将文件直接映射到进程的虚拟内存地址空间,
    //避免了将文件从内核缓冲区拷贝到用户缓冲区的过程（即零拷贝技术的一种），极大提高了静态文件的传输速度
    void* mmRet = (int*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd,0);
    if(mmRet == MAP_FAILED){
        ErrorContent(buff, "File NotFound");
        return;
    }
    mmFile_ = (char*)mmRet;
    close(srcFd);
    //写入 Content-length 头，具体的文件数据本身并没有拷贝进 Buffer，而是通过 mmFile_ 指针后续直接发送
    buff.Append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

string HttpResponse::GetFileType_(){
    /* 判断文件类型 */
    string::size_type idx = path_.find_last_of('.');
    if(idx == string::npos){
        return "text/plain";
    }
    string suffix = path_.substr(idx);
    if(SUFFIX_TYPE.count(suffix) == 1){
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}


void HttpResponse::UnmapFile(){
    if(mmFile_){
        munmap(mmFile_,mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

//当服务器无法读取静态文件（例如文件打开失败或内存映射失败）时，动态生成一个简易的 HTML 错误页面并发送给客户端。
//它是一个“兜底”方案。通常服务器会尝试返回磁盘上的 /404.html 文件，但如果连那个文件读取都出错了，
// 或者在 mmap 过程中发生了严重错误，这个函数就会被调用，直接在内存中拼写一段 HTML 代码返回。
void HttpResponse::ErrorContent(Buffer& buff,string message){
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUE.count(code_) == 1){
        status = CODE_STATUE.find(code_)->second;
    }else{
        status = "Bad Request";
    }
    body += to_string(code_) + " : " + status + "\n";
    body += "<P>" + message + "<P>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}
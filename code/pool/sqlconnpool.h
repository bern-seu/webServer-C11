#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include<mysql/mysql.h>
#include<cstring>
#include<queue>
#include<mutex>
#include<semaphore.h>
#include<thread>
#include"../log/log.h"

class SqlConnPool{
public:
    static SqlConnPool* Instance();
    MYSQL *GetConn();
    void FreeConn(MYSQL* conn);
    int GetFreeConnCount();
    void Init(const char* host, int port,
              const char* user, const char* pwd,
              const char* dbName, int connSize = 10);
    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_;
    int useCount_;
    int freeCpunt_;

    std::queue<MYSQL*> connQue_;
    std::mutex mtx_;
    sem_t semId_;
};

#endif // SQLCONNPOOL_H
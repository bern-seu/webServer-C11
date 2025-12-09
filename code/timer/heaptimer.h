#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h>
#include <functional>
#include <assert.h>
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode{
    int id; //id只是建，真正的序号存在ref_里
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator<(const TimerNode& t){
        return expires < t.expires;
    }
};

class HeapTimer{
public:
    HeapTimer() {heap_.reserve(64);}
    ~HeapTimer() {clear();}
    void adjust(int id, int newExpires);
    void add(int id, int timeout, const TimeoutCallBack& cb);
    void doWork(int id);
    void clear();
    void tick();
    void pop();
    int GetNextTick();

private:
    std::vector<TimerNode> heap_;
    std::unordered_map<int,size_t> ref_;
    void del_(size_t i);
    void siftup_(size_t i);
    bool siftdown_(size_t index,size_t n);
    void SwapNode_(size_t i ,size_t j);
};

#endif //HEAP_TIMER_H
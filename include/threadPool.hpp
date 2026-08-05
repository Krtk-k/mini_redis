#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <functional>

using namespace std;


class ThreadPool {
    int num_threads;
    vector<thread> workers;
    queue<function<void()>> tasks;
    bool end;
    mutex mtx;
    condition_variable cv;

    void worker() {
        while(1) {
            function<void()> task;
            {
                unique_lock<mutex> lock(mtx);
                cv.wait(lock, [this]{return end || !tasks.empty();});
                if(end && tasks.empty()) return;
                task = tasks.front();
                tasks.pop();
            }
            task();
        }
    }
    public:
    ThreadPool(int n) : num_threads(n) {
        end = false;
        for(int i = 0; i<num_threads; i++) {
            workers.push_back(thread([this]{worker();}));
        }
    }
    ~ThreadPool() {
        end = true;
        cv.notify_all();
        for(auto &t : workers) {
            if(t.joinable()) t.join();
        }
    }
    void add_task(function<void()> func) {
        tasks.push(func);
    }
};
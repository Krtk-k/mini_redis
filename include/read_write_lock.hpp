#pragma once
#include <mutex>
#include <condition_variable>

using namespace std;

class Rw_lock {
    condition_variable cv1, cv2;
    mutex mtx;
    int readers, writers;
    bool writing;
    public:
    Rw_lock() {
        readers = 0;
        writers = 0;
        writing = false;
    }
    void acq_read() {
        unique_lock<mutex> lock(mtx);
        cv1.wait(lock, [this]{return writers==0 && !writing;});
        readers++;
    }
    void rel_read() {
        lock_guard<mutex> lock(mtx);
        readers--;
        if(readers == 0) cv2.notify_one();
    }
    void acq_write() {
        unique_lock<mutex> lock(mtx);
        writers++;
        cv2.wait(lock, [this]{return !writing && readers==0;});
        writing = true;
    }
    void rel_write() {
        lock_guard<mutex> lock(mtx);
        writers--;
        writing = false;
        if(writers == 0) cv1.notify_all();
        else cv2.notify_one();
    }
};

class ReadGuard {
    Rw_lock& l;
    public:
    explicit ReadGuard(Rw_lock& lock) :l(lock) {
        l.acq_read();
    }
    ~ReadGuard() {l.rel_read();}
    ReadGuard(const ReadGuard&) = delete;
    ReadGuard& operator = (const ReadGuard&) = delete;
};

class WriteGuard {
    Rw_lock& l;
    public:
    explicit WriteGuard(Rw_lock& lock) :l(lock) {
        l.acq_write();
    }
    ~WriteGuard() {l.rel_write();}
    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator = (const WriteGuard&) = delete;
};

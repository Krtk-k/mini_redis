#pragma once
#include <unordered_map>
#include <vector>
#include "read_write_lock.hpp"

using namespace std;

struct Bucket {
    unordered_map<string, string> mp;
    Rw_lock lock;
};

class ThreadSafeCache {
    vector<unique_ptr<Bucket>> cache;
    int bukets;
    int buck_idx(string &key) {
        hash<string> hasher;
        size_t num = hasher(key);
        return num%this->buckets;
    }
    public:
    ThreadSafeCache(int n) : buckets {
        for(int i = 0; i<buckets; i++) {
            cache.push_back(make_unique<Bucket>());
        }
    }
    string get(string key) {
        int b_num = buck_idx(key);
        unique_ptr<Bucket> curr = cache[b_num];
        curr->lock.acq_read();
        if(curr->mp.find(key) == curr->mp.end()) return "NULL";
        else return curr->mp[key];
        curr->lock.rel_read();
    }
    void set(string key, string val) {
        int b_num = buck_idx(key);
        int b_num = buck_idx(key);
        unique_ptr<Bucket> curr = cache[b_num];
        curr->lock.acq_write();
        curr->mp[key] = val;
        curr->lock.rel_write();
    }
    int get_size() {
        size_t size = 0;
        for(int i = 0; i<buckets; i++) {
            cache[i]->lock.acq_write();
            size += cache[i]->mp.size();
            cache[i]->lock.rel_write();
        }
        return size;
    }
};
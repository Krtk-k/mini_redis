#pragma once
#include <unordered_map>
#include <vector>
#include <memory>

#include "read_write_lock.hpp"

using namespace std;

struct Bucket {
    unordered_map<string, string> mp;
    Rw_lock lock;
};

class ThreadSafeCache {
    vector<unique_ptr<Bucket>> cache;
    int buckets;
    int buck_idx(string &key) {
        hash<string> hasher;
        size_t num = hasher(key);
        return num%this->buckets;
    }
    public:
    ThreadSafeCache(int n) : buckets(n) {
        for(int i = 0; i<buckets; i++) {
            cache.push_back(make_unique<Bucket>());
        }
    }
    string get(string key) {
        Bucket& curr = *cache[buck_idx(key)];
        ReadGuard guard(curr.lock);
        auto it = curr.mp.find(key);
        if(it == curr.mp.end()) return "NULL";
        return it->second;
    }
    void set(string key, string val) {
        Bucket& curr = *cache[buck_idx(key)];
        WriteGuard guard(curr.lock);
        curr.mp[key] = val;
    }
    size_t get_size() {
        size_t size = 0;
        for(int i = 0; i<buckets; i++) {
            ReadGuard guard(cache[i]->lock);
            size += cache[i]->mp.size();
        }
        return size;
    }
};
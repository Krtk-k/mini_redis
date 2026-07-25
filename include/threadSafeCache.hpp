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
    int add_calc(int )
    public:
    ThreadSafeCache(int n) : buckets {
        for(int i = 0; i<buckets; i++) {
            cahce.push_back(make_unique<Bucket>());
        }
    }

};
#pragma once
#include "../validation_e3_20260906/data.hpp"
#include "vendor/lits/LITS/lits/lits.hpp"
#include "vendor/libart/src/art.h"

namespace e4 {
using namespace validation;
inline void ascii_key(const std::string& k) {
    if(k.empty())throw std::invalid_argument("empty native key");
    for(unsigned char c:k)if(c==0||c>127)throw std::invalid_argument("native protocol requires ASCII without NUL");
}
class NativeLits {
    lits::LITS index_; bool built_=false;
public:
    explicit NativeLits(const std::vector<Pair>& data){
        std::vector<const char*> keys;std::vector<std::uint64_t> vals;
        for(const auto&kv:data){ascii_key(kv.first);keys.push_back(kv.first.c_str());vals.push_back(kv.second);}
        built_=index_.bulkload(keys.data(),vals.data(),static_cast<int>(keys.size()));
        if(!built_)throw std::runtime_error("LITS bulkload failed");
    }
    NativeLits(const NativeLits&)=delete;
    ~NativeLits(){if(built_)index_.destroy();}
    Result find(const std::string&key){auto*p=index_.lookup(key.c_str());return p?Result{true,p->read()}:Result{};}
    void upsert(const std::string&key,std::uint64_t v){index_.upsert(key.c_str(),v);}
    bool erase(const std::string&key){return index_.remove(key.c_str());}
};
class NativeArt {
    art_tree tree_{};
public:
    explicit NativeArt(const std::vector<Pair>&data){art_tree_init(&tree_);for(const auto&kv:data){ascii_key(kv.first);upsert(kv.first,kv.second);}}
    NativeArt(const NativeArt&)=delete;
    ~NativeArt(){art_tree_destroy(&tree_);}
    // Include the terminator to give prefix keys an explicit terminal edge.
    // Values + 1 are opaque, never dereferenced; UINT64_MAX is excluded.
    Result find(const std::string&key){auto*p=art_search(&tree_,reinterpret_cast<const unsigned char*>(key.c_str()),key.size()+1);return p?Result{true,reinterpret_cast<std::uintptr_t>(p)-1}:Result{};}
    void upsert(const std::string&key,std::uint64_t v){if(v==UINT64_MAX)throw std::invalid_argument("ART adapter reserved value");art_insert(&tree_,reinterpret_cast<const unsigned char*>(key.c_str()),key.size()+1,reinterpret_cast<void*>(static_cast<std::uintptr_t>(v+1)));}
    bool erase(const std::string&key){return art_delete(&tree_,reinterpret_cast<const unsigned char*>(key.c_str()),key.size()+1)!=nullptr;}
};
}

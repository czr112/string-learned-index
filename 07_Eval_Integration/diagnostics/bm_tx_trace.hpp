#pragma once
// Shared bounded reader for the benchmark's SIBROW1 / SIBOPS1 interchange.
#include "index_contract.h"
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

namespace bm_tx_trace {
namespace sib=string_index_bench::v1;
class Reader {
    std::ifstream input_;
    std::uint64_t remaining_;
public:
    explicit Reader(const char* path):input_(path,std::ios::binary|std::ios::ate) {
        if(!input_)throw std::runtime_error("cannot open interchange file");
        remaining_=std::uint64_t(input_.tellg());input_.seekg(0);
    }
    void bytes(void* p,std::size_t n) {
        if(n>remaining_)throw std::runtime_error("truncated interchange");
        if(n&&!input_.read(static_cast<char*>(p),std::streamsize(n)))throw std::runtime_error("interchange read failed");
        remaining_-=n;
    }
    std::uint64_t integer() {
        unsigned char b[8];bytes(b,8);std::uint64_t value=0;
        for(unsigned i=0;i<8;++i)value|=std::uint64_t(b[i])<<(8*i);
        return value;
    }
    std::string key() {
        const auto n=integer();if(n>remaining_||n>UINT32_MAX)throw std::runtime_error("invalid interchange key length");
        std::string out(std::size_t(n),'\0');bytes(out.data(),out.size());return out;
    }
    std::uint64_t header(const char* magic) {
        char got[8];bytes(got,8);
        if(std::memcmp(got,magic,8))throw std::runtime_error("invalid interchange magic");
        auto n=integer();if(n>remaining_)throw std::runtime_error("invalid interchange count");return n;
    }
    void finish(){if(remaining_)throw std::runtime_error("trailing interchange data");}
};
struct Operation {std::uint8_t kind;std::uint64_t value,limit;std::string key;};
inline std::vector<sib::Row> rows(const char* path) {
    Reader r(path);const auto n=r.header("SIBROW1\0");std::vector<sib::Row> out;out.reserve(n);
    for(std::uint64_t i=0;i<n;++i){auto key=r.key();const auto value=r.integer();out.push_back({std::move(key),value});}
    r.finish();return out;
}
inline std::vector<Operation> operations(const char* path) {
    Reader r(path);const auto n=r.header("SIBOPS1\0");std::vector<Operation> out;out.reserve(n);
    for(std::uint64_t i=0;i<n;++i) {
        Operation op;r.bytes(&op.kind,1);op.value=r.integer();op.limit=r.integer();op.key=r.key();
        if(op.kind<1||op.kind>5)throw std::runtime_error("invalid operation kind");
        out.push_back(std::move(op));
    }
    r.finish();return out;
}
inline std::uint64_t apply(sib::Index& index,const Operation& op) {
    switch(op.kind) {
    case 1:return index.find(op.key).value_or(UINT64_MAX);
    case 2:return index.insert(op.key,op.value)==sib::InsertResult::inserted?1:2;
    case 3:return index.update(op.key,op.value)==sib::UpdateResult::updated?3:4;
    case 4:return index.erase(op.key)==sib::EraseResult::erased?5:6;
    case 5:{auto out=index.scan(op.key,op.limit);return out.size();}
    }
    throw std::logic_error("unreachable operation");
}
} // namespace bm_tx_trace

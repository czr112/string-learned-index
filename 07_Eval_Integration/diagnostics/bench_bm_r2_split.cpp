// R2: attribute growth-insert cost on the R1 traces - segment split plus rebuild arithmetic.
#include "bm_timed_index.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
namespace {
struct Operation {std::uint8_t kind=0;std::uint64_t value=0,limit=0;std::string key;};

std::vector<std::pair<std::string,std::uint64_t>> read_rows(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot open rows");
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBROW1\0",8)!=0)throw std::runtime_error("bad rows magic");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<std::pair<std::string,std::uint64_t>> rows;rows.reserve(count);
    for(std::uint64_t i=0;i<count;++i) {
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        std::string key=bytes.substr(pos,n);pos+=n;
        std::uint64_t value=0;std::memcpy(&value,bytes.data()+pos,8);pos+=8;
        rows.emplace_back(std::move(key),value);
    }
    return rows;
}
std::vector<Operation> read_operations(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("cannot open operations");
    std::string bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    if(bytes.size()<16||bytes.compare(0,8,"SIBOPS1\0",8)!=0)throw std::runtime_error("bad operations magic");
    std::uint64_t count=0;std::memcpy(&count,bytes.data()+8,8);
    std::size_t pos=16;std::vector<Operation> operations;operations.reserve(count);
    for(std::uint64_t i=0;i<count;++i) {
        Operation op;op.kind=std::uint8_t(bytes[pos]);++pos;
        std::memcpy(&op.value,bytes.data()+pos,8);pos+=8;
        std::memcpy(&op.limit,bytes.data()+pos,8);pos+=8;
        std::uint64_t n=0;std::memcpy(&n,bytes.data()+pos,8);pos+=8;
        op.key=bytes.substr(pos,n);pos+=n;operations.push_back(std::move(op));
    }
    return operations;
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin operations.bin label");
        const std::string label=argv[3];
        const auto initial=read_rows(argv[1]);
        const auto operations=read_operations(argv[2]);
        std::vector<static_physical::Row> physical;physical.reserve(initial.size());
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& [key,value]:initial) {
            physical.push_back({key,value});
            oracle.emplace(key,value);
        }
        bm_timed::Index index(physical);
        const auto before=index.maintenance();
        const auto pages_before=index.page_count();
        const auto batch_start=Clock::now();
        std::uint64_t inserted=0,mismatches=0,find_ops=0,find_ns=0;
        for(const auto& op:operations) {
            if(op.kind==1) {
                const auto start=Clock::now();
                const auto want=oracle.find(op.key);
                const auto got=index.find_base(op.key);
                if((want==oracle.end())!=!got||(got&&want!=oracle.end()&&*got!=want->second))++mismatches;
                find_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                ++find_ops;
            } else if(op.kind==2) {
                const bool present=oracle.find(op.key)!=oracle.end();
                const bool ok=index.insert(op.key,op.value);
                if(ok==present)++mismatches;
                if(ok){oracle.emplace(op.key,op.value);++inserted;}
            } else throw std::invalid_argument("growth only");
        }
        const auto batch_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-batch_start).count());
        const auto after=index.maintenance();
        const auto rebuilds=after.rebuilds-before.rebuilds;
        const auto rebuilt_rows=after.rows_rebuilt-before.rows_rebuilt;
        const auto root_ns=index.root_ns(),page_ns=index.page_ns(),reclaim_ns=index.reclaim_ns();
        const auto inserts=after.inplace_inserts-before.inplace_inserts;
        std::cout<<"{\"label\":\""<<label<<"\",\"initial_rows\":"<<initial.size()
            <<",\"pages_before\":"<<pages_before<<",\"pages_after\":"<<index.page_count()
            <<",\"inserted\":"<<inserted<<",\"inplace\":"<<inserts<<",\"rebuilds\":"<<rebuilds
            <<",\"rows_rebuilt\":"<<rebuilt_rows
            <<",\"rows_per_rebuild\":"<<(rebuilds?double(rebuilt_rows)/rebuilds:0.0)
            <<",\"page_rows\":2048,\"slots\":16,\"occupancy\":15"
            <<",\"mismatches\":"<<mismatches
            <<",\"batch_ns\":"<<batch_ns<<",\"find_ns\":"<<find_ns
            <<",\"page_ns\":"<<page_ns<<",\"root_ns\":"<<root_ns<<",\"reclaim_ns\":"<<reclaim_ns
            <<",\"write_ns\":"<<(batch_ns>find_ns?batch_ns-find_ns:0)
            <<",\"page_share_of_write\":"<<((batch_ns>find_ns&&batch_ns-find_ns)?double(page_ns)/(batch_ns-find_ns):0.0)
            <<",\"root_share_of_write\":"<<((batch_ns>find_ns&&batch_ns-find_ns)?double(root_ns)/(batch_ns-find_ns):0.0)
            <<",\"reclaim_share_of_write\":"<<((batch_ns>find_ns&&batch_ns-find_ns)?double(reclaim_ns)/(batch_ns-find_ns):0.0)
            <<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

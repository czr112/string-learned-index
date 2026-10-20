// P1: separate the real costs of root preparation, page construction and reclamation.
// Fixed page count, fixed insert batch; the three segment timers come from the instrumented copy.
#include "bm_timed_index.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
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
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin inserts");
        const std::size_t inserts=std::stoul(argv[2]);
        const auto rows=read_rows(argv[1]);
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& [key,value]:rows)physical.push_back({key,value});
        bm_timed::Index index(physical);
        const auto before=index.maintenance();
        std::uint64_t inserted=0,mismatches=0;
        const auto batch_start=Clock::now();
        for(std::size_t i=0;i<inserts;++i) {
            const auto source=(i*rows.size())/inserts;
            std::string key=rows[source].first;key+="~grow/"+std::to_string(i);
            if(index.insert(key,1'000'000+i))++inserted;else ++mismatches;
        }
        const auto batch_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now()-batch_start).count());
        const auto after=index.maintenance();
        const auto root_ns=index.root_ns(),page_ns=index.page_ns(),reclaim_ns=index.reclaim_ns();
        const auto other_ns=batch_ns>root_ns+page_ns+reclaim_ns?batch_ns-(root_ns+page_ns+reclaim_ns):0;
        std::cout<<"{\"rows\":"<<rows.size()            <<",\"inserts\":"<<inserts<<",\"inserted\":"<<inserted<<",\"mismatches\":"<<mismatches
            <<",\"rebuilds\":"<<after.rebuilds-before.rebuilds
            <<",\"publications\":"<<after.root_publications-before.root_publications
            <<",\"rows_rebuilt\":"<<after.rows_rebuilt-before.rows_rebuilt
            <<",\"entries_copied\":"<<after.root_entries_copied-before.root_entries_copied
            <<",\"batch_ns\":"<<batch_ns<<",\"root_ns\":"<<root_ns<<",\"page_ns\":"<<page_ns
            <<",\"reclaim_ns\":"<<reclaim_ns<<",\"other_ns\":"<<other_ns
            <<",\"root_share\":"<<(batch_ns?double(root_ns)/batch_ns:0.0)
            <<",\"page_share\":"<<(batch_ns?double(page_ns)/batch_ns:0.0)
            <<",\"reclaim_share\":"<<(batch_ns?double(reclaim_ns)/batch_ns:0.0)
            <<",\"other_share\":"<<(batch_ns?double(other_ns)/batch_ns:0.0)
            <<",\"pages\":"<<index.page_count()<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Stage-5 root cost: does the root copy grow with page count?
// Builds the BM page from a frozen row set, inserts a fixed synthetic batch, and reports the
// maintenance ledger (pages, publications, entries and key bytes copied) plus the batch time.
#include "bm_bounded_index.hpp"
#include <chrono>
#include <cstdint>
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
        if(argc!=4)throw std::invalid_argument("rows.bin inserts count_label");
        const std::string label=argv[3];
        const std::size_t inserts=std::stoul(argv[2]);
        const auto rows=read_rows(argv[1]);
        if(rows.empty()||inserts==0||inserts>=rows.size())throw std::invalid_argument("bad sizes");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& [key,value]:rows)physical.push_back({key,value});
        const auto build_start=Clock::now();
        bm_bounded_page::Index index(physical);
        const auto build_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-build_start).count());
        const auto pages=index.page_count();
        const auto before=index.maintenance();
        // Evenly spaced source keys, each extended by a suffix: absent, same page, sorted later.
        std::uint64_t mismatches=0,inserted=0;
        std::vector<double> pause_ns;pause_ns.reserve(inserts);
        const auto batch_start=Clock::now();
        for(std::size_t i=0;i<inserts;++i) {
            const auto source=(i*rows.size())/inserts;
            std::string key=rows[source].first;key+="~grow/"+std::to_string(i);
            const auto start=Clock::now();
            const bool ok=index.insert(key,1'000'000+i);
            pause_ns.push_back(double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()));
            if(ok)++inserted;else ++mismatches;
        }
        const auto batch_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-batch_start).count());
        const auto after=index.maintenance();
        std::sort(pause_ns.begin(),pause_ns.end());
        auto at=[&](double q) {return pause_ns[std::size_t(double(pause_ns.size()-1)*q)];};
        const auto publications=after.root_publications-before.root_publications;
        const auto entries=after.root_entries_copied-before.root_entries_copied;
        const auto key_bytes=after.root_key_bytes_copied-before.root_key_bytes_copied;
        const auto rebuilds=after.rebuilds-before.rebuilds;
        std::cout<<"{\"label\":\""<<label<<"\",\"rows\":"<<rows.size()<<",\"pages\":"<<pages
            <<",\"inserts\":"<<inserts<<",\"inserted\":"<<inserted<<",\"mismatches\":"<<mismatches
            <<",\"publications\":"<<publications<<",\"rebuilds\":"<<rebuilds
            <<",\"entries_copied\":"<<entries<<",\"key_bytes_copied\":"<<key_bytes
            <<",\"entries_per_publication\":"<<(publications?double(entries)/publications:0.0)
            <<",\"largest_root_entries\":"<<after.largest_root_entries
            <<",\"pages_after\":"<<index.page_count()
            <<",\"build_ns\":"<<build_ns<<",\"batch_ns\":"<<batch_ns
            <<",\"pause_ns\":{\"p50\":"<<at(0.50)<<",\"p95\":"<<at(0.95)<<",\"p99\":"<<at(0.99)<<",\"max\":"<<at(1.0)<<"}"
            <<",\"memory\":"<<index.memory_bytes()<<"}\n";
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

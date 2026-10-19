// Stage-5 read budget: replay a frozen trace to its final map, rebuild a read-only snapshot from
// that same map, and compare complete point finds on both. Also reports root copy work and the
// per-operation pause distribution of the replay.
#include "bm_bounded_index.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
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
std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
std::vector<std::uint32_t> permutation(std::size_t n,std::uint64_t repeat) {
    std::vector<std::uint32_t> p(n);std::iota(p.begin(),p.end(),std::uint32_t(0));
    std::mt19937_64 rng(UINT64_C(0x9e3779b97f4a7c15)^(repeat*UINT64_C(0x100000001b3))^std::uint64_t(n));
    std::shuffle(p.begin(),p.end(),rng);return p;
}
double percentile(std::vector<double> values,double q) {
    if(values.empty())return 0;
    std::sort(values.begin(),values.end());
    return values[std::size_t(double(values.size()-1)*q)];
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("initial_rows.bin operations.bin process_offset");
        const int offset=std::stoi(argv[3]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        const auto rows=read_rows(argv[1]);const auto operations=read_operations(argv[2]);
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        std::map<std::string,std::uint64_t> oracle;
        for(const auto& [key,value]:rows){physical.push_back({key,value});oracle.emplace(key,value);}
        bm_bounded_page::Index dynamic(physical);
        std::vector<double> pause_ns;pause_ns.reserve(operations.size());
        std::uint64_t mismatches=0,insert_ops=0;
        const auto replay_start=Clock::now();
        for(const auto& op:operations) {
            const auto start=Clock::now();
            switch(op.kind) {
                case 1: {
                    const auto want=oracle.find(op.key);
                    const auto got=dynamic.find_base(op.key);
                    if((want==oracle.end())!=!got||(got&&want!=oracle.end()&&*got!=want->second))++mismatches;
                    break;
                }
                case 2: {
                    const bool present=oracle.find(op.key)!=oracle.end();
                    const bool inserted=dynamic.insert(op.key,op.value);
                    if(inserted==present)++mismatches;
                    if(inserted)oracle.emplace(op.key,op.value);
                    ++insert_ops;break;
                }
                case 3: {
                    const bool present=oracle.find(op.key)!=oracle.end();
                    if(dynamic.update(op.key,op.value)!=present)++mismatches;
                    if(present)oracle[op.key]=op.value;break;
                }
                case 4: {
                    const bool present=oracle.find(op.key)!=oracle.end();
                    if(dynamic.erase(op.key)!=present)++mismatches;
                    if(present)oracle.erase(op.key);break;
                }
                default: {
                    const auto got=dynamic.scan(op.key,op.limit);
                    auto it=oracle.lower_bound(op.key);std::size_t n=0;
                    for(;it!=oracle.end()&&n<op.limit;++it,++n)
                        if(n>=got.size()||got[n].key!=it->first||got[n].value!=it->second){++mismatches;break;}
                    if(got.size()!=n)++mismatches;break;
                }
            }
            pause_ns.push_back(double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()));
            if(dynamic.size()!=oracle.size())++mismatches;
        }
        const auto replay_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-replay_start).count());
        // Snapshot from the same final map, through the same bounded constructor.
        std::vector<static_physical::Row> final_rows;final_rows.reserve(oracle.size());
        for(const auto& [key,value]:oracle)final_rows.push_back({key,value});
        const auto snapshot_start=Clock::now();
        bm_bounded_page::Index snapshot(final_rows);
        const auto snapshot_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-snapshot_start).count());
        if(dynamic.size()!=snapshot.size())throw std::logic_error("snapshot size mismatch");

        // Both must answer identically on every key of the final map plus absent probes.
        std::vector<std::string> keys;keys.reserve(oracle.size());
        for(const auto& [key,value]:oracle)keys.push_back(key);
        std::uint64_t checked=0;
        for(const auto& key:keys) {
            if(dynamic.find_base(key)!=snapshot.find_base(key))++mismatches;
            ++checked;
        }
        std::mt19937_64 probe_rng(20261019);
        for(int i=0;i<2000;++i) {
            std::string absent=keys[probe_rng()%keys.size()]+"~absent";
            if(oracle.count(absent))continue;
            if(dynamic.find_base(absent)!=snapshot.find_base(absent))++mismatches;
            ++checked;
        }

        const char* names[]={"dynamic","dynamic_AA","snapshot","snapshot_AA"};
        constexpr int williams[]={0,1,3,2};
        auto pass=[&](int arm,const std::vector<std::uint32_t>& order) {
            std::uint64_t c=0;
            const auto& index=(arm<2)?dynamic:snapshot;
            for(const auto i:order)c=c*131+token(index.find_base(keys[i]));
            return c;
        };
        for(int arm=0;arm<4;++arm)pass(arm,permutation(keys.size(),0));
        std::cout<<"{\"phase\":\"gate\",\"initial_rows\":"<<rows.size()<<",\"operations\":"<<operations.size()
            <<",\"final_rows\":"<<keys.size()<<",\"insert_ops\":"<<insert_ops<<",\"mismatches\":"<<mismatches
            <<",\"checked\":"<<checked<<",\"replay_ns\":"<<replay_ns<<",\"snapshot_build_ns\":"<<snapshot_ns
            <<",\"pause_ns\":{\"p50\":"<<percentile(pause_ns,0.50)<<",\"p95\":"<<percentile(pause_ns,0.95)
            <<",\"p99\":"<<percentile(pause_ns,0.99)<<",\"max\":"<<percentile(pause_ns,1.0)<<"}"
            <<",\"maintenance\":{\"pages\":"<<dynamic.page_count()
            <<",\"root_publications\":"<<dynamic.maintenance().root_publications
            <<",\"root_entries_copied\":"<<dynamic.maintenance().root_entries_copied
            <<",\"root_key_bytes_copied\":"<<dynamic.maintenance().root_key_bytes_copied
            <<",\"largest_root_entries\":"<<dynamic.maintenance().largest_root_entries
            <<",\"rebuilds\":"<<dynamic.maintenance().rebuilds
            <<",\"rows_rebuilt\":"<<dynamic.maintenance().rows_rebuilt
            <<",\"inplace_inserts\":"<<dynamic.maintenance().inplace_inserts<<"}"
            <<",\"memory\":{\"dynamic\":"<<dynamic.memory_bytes()<<",\"snapshot\":"<<snapshot.memory_bytes()<<"}"
            <<"}\n";
        std::cout.flush();
        for(std::size_t repeat=0;repeat<6;++repeat) {
            const auto order=permutation(keys.size(),repeat);
            std::uint64_t checksums[4]={};
            for(int position=0;position<4;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%4;
                const auto start=Clock::now();
                const auto checksum=pass(arm,order);
                const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                checksums[arm]=checksum;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position
                    <<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<keys.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
            for(int arm=1;arm<4;++arm)if(checksums[arm]!=checksums[0])throw std::logic_error("arm checksum differs");
        }
        return mismatches?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

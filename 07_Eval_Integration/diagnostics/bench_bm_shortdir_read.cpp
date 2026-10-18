// Full read gate for the short-directory candidate: domain correctness, then a complete-find
// timing that includes the real routing layer. Arms share one input pair and one permutation.
#include "bm_shortdir_index.hpp"
#include "bm_page_index.hpp"
#include "benchmark/factories.h"
#include "coupled_page_adapter.hpp"
#include "../adapters/art/art_adapter.h"
#include "static_input.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;
using Clock=std::chrono::steady_clock;
static volatile std::uint64_t sink=0;

static std::uint64_t token(std::optional<std::uint64_t> r) {
    return r?*r^UINT64_C(0x9e3779b97f4a7c15):UINT64_C(0xd1b54a32d192ed03);
}
static std::vector<std::uint32_t> permutation(std::size_t n,std::uint64_t repeat) {
    std::vector<std::uint32_t> p(n);std::iota(p.begin(),p.end(),std::uint32_t(0));
    std::mt19937_64 rng(UINT64_C(0x9e3779b97f4a7c15)^(repeat*UINT64_C(0x100000001b3))^std::uint64_t(n));
    std::shuffle(p.begin(),p.end(),rng);return p;
}
namespace {
class PageArm final:public sib::Index {
    std::unique_ptr<bm_page::Index> index_;
public:
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;d.implementation="BM-page-window";d.implementation_version="bm_page_index.hpp";
        d.adapter_version="bench_bm_shortdir_read.cpp";
        auto& c=d.capabilities;c.bulk_load=c.find=c.insert=c.update=c.erase=c.scan=sib::Support::native;
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});
        index_=std::make_unique<bm_page::Index>(physical);
    }
    std::optional<sib::Value> find(std::string_view key)const override {return index_->find(key);}
    sib::InsertResult insert(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("read-only arm");}
    sib::UpdateResult update(std::string_view,std::uint64_t)override{throw sib::UnsupportedOperation("read-only arm");}
    sib::EraseResult erase(std::string_view)override{throw sib::UnsupportedOperation("read-only arm");}
    sib::Value range_sum(std::string_view,std::string_view)const override{throw sib::UnsupportedOperation("range_sum");}
    std::vector<sib::Row> scan(std::string_view lower,std::size_t limit)const override {
        auto rows=index_->scan(lower,limit);std::vector<sib::Row> out;out.reserve(rows.size());
        for(auto& r:rows)out.push_back({std::move(r.key),r.value});return out;
    }
    sib::MemoryUsage memory_usage()const override {
        sib::MemoryUsage m;m.index_owned_bytes=index_?index_->memory_bytes():0;
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;return m;
    }
    std::optional<std::uint64_t> base(std::string_view key)const {return index_->find_base(key);}
};
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=5)throw std::invalid_argument("rows.bin queries.bin mode(domain|time) process_offset");
        const std::string mode=argv[3];
        if(mode!="domain"&&mode!="time")throw std::invalid_argument("unknown mode");
        const int offset=std::stoi(argv[4]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        std::map<std::string,std::uint64_t> reference;
        for(const auto& r:rows){physical.push_back({r.key,r.value});reference.emplace(r.key,r.value);}
        std::vector<uni::Query> mixed;mixed.reserve(queries.size());
        for(std::size_t i=0;i<queries.size();++i) {
            if(i%10==9) {
                std::string key=queries[i].key;key+="~f2/miss";
                while(reference.count(key))key+="~";
                mixed.push_back({std::move(key),false,0});
            } else mixed.push_back(queries[i]);
        }

        const auto build_start=Clock::now();
        bm_sdindex::Index candidate(physical);
        const auto candidate_ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-build_start).count());

        if(mode=="domain") {
            // Edge probes cover every byte class, prefix/successor relations and absent gaps.
            std::vector<std::string> probes{"",std::string(1,'\0'),std::string(1,char(255)),
                "a",std::string("a\0b",3),std::string(4096,'z'),"~","\x80\x81\x82",
                std::string(1,char(128))+std::string(1,char(255))};
            for(std::size_t i=0;i<physical.size();++i) {
                const auto& key=physical[i].key;
                if(i<512) {
                    probes.push_back(key+std::string(1,'\0'));
                    probes.push_back(key+std::string(1,char(255)));
                    if(!key.empty())probes.push_back(key.substr(0,key.size()-1));
                }
            }
            std::uint64_t checks=0,mismatches=0;
            auto expect=[&](std::string_view key) {
                const auto found=reference.find(std::string(key));
                const auto want=found==reference.end()?std::optional<std::uint64_t>{}:
                    std::optional<std::uint64_t>(found->second);
                if(candidate.find(key)!=want)++mismatches;
                ++checks;
            };
            for(const auto& probe:probes)expect(probe);
            for(const auto& row:physical)expect(row.key);
            for(const auto& q:mixed)expect(q.key);
            // Scan must equal the reference for sampled starts and for the whole library.
            std::uint64_t scans=0;
            auto scan_expect=[&](std::string_view lower,std::size_t limit) {
                const auto got=candidate.scan(lower,limit);
                auto it=reference.lower_bound(std::string(lower));
                std::size_t n=0;
                for(;it!=reference.end()&&n<limit;++it,++n) {
                    if(n>=got.size()||got[n].key!=it->first||got[n].value!=it->second){++mismatches;break;}
                }
                if(got.size()!=n)++mismatches;
                ++scans;
            };
            for(const auto& probe:probes)scan_expect(probe,17);
            scan_expect("",SIZE_MAX);
            scan_expect(std::string(1,'\0'),SIZE_MAX);
            if(mismatches)throw std::logic_error("domain mismatch");
            std::cout<<"{\"mode\":\"domain\",\"rows\":"<<physical.size()<<",\"queries\":"<<mixed.size()
                <<",\"probes\":"<<probes.size()<<",\"find_checks\":"<<checks<<",\"scans\":"<<scans
                <<",\"checks\":"<<checks+scans<<",\"mismatches\":"<<mismatches
                <<",\"pages\":"<<candidate.page_count()
                <<",\"memory\":{\"total\":"<<candidate.memory()
                <<",\"short_directory\":"<<candidate.short_directory_bytes()
                <<",\"fence_views\":"<<candidate.fence_view_bytes()<<"}"
                <<",\"build_ns\":"<<candidate_ns<<"}\n";
            return 0;
        }

        // time: eight arms over the same queries.
        std::unique_ptr<bm_page::Index> page;
        {std::vector<static_physical::Row> copy=physical;page=std::make_unique<bm_page::Index>(copy);}
        std::unique_ptr<sib::Index> native=make_block_residual_delta_shortdir_first({});
        native->bulk_load(rows);
        std::unique_ptr<sib::Index> f5=make_coupled_page_index({});
        f5->bulk_load(rows);
        std::unique_ptr<sib::Index> art=make_art_index({});
        art->bulk_load(rows);
        const char* names[]={"shortdir","shortdir_AA","bm-page-window","bm-page-window_AA",
                             "bm-page-base","native-shortdir-first","ART","F5"};
        auto find_arm=[&](int arm,std::string_view key)->std::optional<std::uint64_t> {
            switch(arm) {
                case 0:case 1:return candidate.find(key);
                case 2:case 3:return page->find(key);
                case 4:return page->find_base(key);
                case 5:return native->find(key);
                case 6:return art->find(key);
                default:return f5->find(key);
            }
        };
        std::uint64_t verified=0;
        for(int arm=0;arm<8;++arm) {
            for(const auto& row:rows) {
                if(find_arm(arm,row.key)!=std::optional<sib::Value>(row.value))
                    throw std::logic_error(std::string("row oracle: ")+names[arm]);
                ++verified;
            }
            for(const auto& q:mixed) {
                const auto found=find_arm(arm,q.key);
                if(found.has_value()!=q.expected_present||(found&&*found!=q.expected_value))
                    throw std::logic_error(std::string("query oracle: ")+names[arm]);
                ++verified;
            }
            std::cout<<"{\"phase\":\"gate\",\"arm\":\""<<names[arm]<<"\",\"verified\":"<<rows.size()+mixed.size()
                <<",\"rows\":"<<rows.size()<<",\"queries\":"<<mixed.size()<<"}\n";
        }
        std::cout<<"{\"phase\":\"objects\",\"pages\":"<<candidate.page_count()
            <<",\"memory\":{\"candidate\":"<<candidate.memory()
            <<",\"short_directory\":"<<candidate.short_directory_bytes()
            <<",\"fence_views\":"<<candidate.fence_view_bytes()<<"}"
            <<",\"bm_page\":"<<page->memory_bytes()<<"}\n";
        std::cout.flush();
        constexpr int williams[]={0,1,7,2,6,3,5,4};
        for(std::size_t repeat=0;repeat<6;++repeat) {
            const auto order=permutation(mixed.size(),repeat);
            std::uint64_t checksums[8]={};
            for(int position=0;position<8;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%8;
                const auto start=Clock::now();
                std::uint64_t checksum=0;
                for(const auto i:order)checksum=checksum*131+token(find_arm(arm,mixed[i].key));
                const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                checksums[arm]=checksum;sink=checksum;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position
                    <<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<mixed.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
            for(int arm=1;arm<8;++arm)if(checksums[arm]!=checksums[0])throw std::logic_error("arm checksum differs");
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

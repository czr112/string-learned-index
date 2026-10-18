// Phase 1 of the BM long-term plan: connect the upstream complete BlockLearn factories and
// compare them with this project's BM page, F5 bridge and ART on the same frozen inputs.
// All arms share the new benchmark contract (003a55bf); our own page/F5 code compiles against it
// exactly as the BM round already did.
#include "benchmark/factories.h"
#include "bm_page_index.hpp"
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
// Thin v1 wrapper so the BM page can be measured on both its window-corrected and its base path
// without touching the frozen adapter package.
class PageArm final:public sib::Index {
    std::unique_ptr<bm_page::Index> index_;
    bool base_;
public:
    explicit PageArm(bool base):base_(base){}
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;d.implementation=base_?"BM-page-base":"BM-page-window";
        d.implementation_version="bm_page_index.hpp";d.adapter_version="bench_bm_native.cpp";
        auto& c=d.capabilities;c.bulk_load=c.find=c.insert=c.update=c.erase=c.scan=sib::Support::native;
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});
        index_=std::make_unique<bm_page::Index>(physical);
    }
    std::optional<sib::Value> find(std::string_view key)const override {
        if(!index_)throw std::logic_error("bulk_load required");
        return base_?index_->find_base(key):index_->find(key);
    }
    sib::InsertResult insert(std::string_view key,sib::Value value)override {
        return index_->insert(key,value)?sib::InsertResult::inserted:sib::InsertResult::already_exists;
    }
    sib::UpdateResult update(std::string_view key,sib::Value value)override {
        return index_->update(key,value)?sib::UpdateResult::updated:sib::UpdateResult::not_found;
    }
    sib::EraseResult erase(std::string_view key)override {
        return index_->erase(key)?sib::EraseResult::erased:sib::EraseResult::not_found;
    }
    sib::Value range_sum(std::string_view,std::string_view)const override {
        throw sib::UnsupportedOperation("range_sum unsupported");
    }
    std::vector<sib::Row> scan(std::string_view lower,std::size_t limit)const override {
        auto rows=index_->scan(lower,limit);std::vector<sib::Row> out;out.reserve(rows.size());
        for(auto& r:rows)out.push_back({std::move(r.key),r.value});return out;
    }
    sib::MemoryUsage memory_usage()const override {
        sib::MemoryUsage m;m.index_owned_bytes=index_?index_->memory_bytes():0;
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;
        m.accounting_notes="BM page self-report; excludes allocator, peak and root copies.";return m;
    }
};
}

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin queries.bin process_offset");
        const int offset=std::stoi(argv[3]);if(offset<0||offset>4)throw std::invalid_argument("offset");
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});
        // Every tenth query becomes a proved-absent suffix key, the same shape the BM real round
        // used, so each cell mixes hits and absences instead of replaying only present keys.
        std::vector<uni::Query> mixed;mixed.reserve(queries.size());
        std::map<std::string,std::uint64_t> reference;
        for(const auto& r:rows)reference.emplace(r.key,r.value);
        for(std::size_t i=0;i<queries.size();++i) {
            if(i%10==9) {
                std::string key=queries[i].key;key+="~f2/miss";
                while(reference.count(key))key+="~";
                mixed.push_back({std::move(key),false,0});
            } else mixed.push_back(queries[i]);
        }
        for(const auto& q:mixed) {
            const auto found=reference.find(q.key);
            if(found!=reference.end()!=q.expected_present)throw std::logic_error("query oracle disagrees with rows");
            if(q.expected_present&&found->second!=q.expected_value)throw std::logic_error("query value disagrees");
        }

        const char* names[]={"native-delta","native-delta_AA","native-shortdir-first","native-shortdir-first_AA",
                             "bm-page-window","bm-page-window_AA","bm-page-base","F5","ART"};
        const sib::Factory native_delta=make_block_residual_delta;
        const sib::Factory native_sdf=make_block_residual_delta_shortdir_first;
        std::unique_ptr<sib::Index> objects[6];std::uint64_t builds[6]{};int build_positions[6]{};
        auto build=[&](int slot) {
            const auto start=Clock::now();
            switch(slot) {
                case 0:objects[slot]=native_delta({});break;
                case 1:objects[slot]=native_sdf({});break;
                case 2:objects[slot]=std::make_unique<PageArm>(false);break;
                case 3:objects[slot]=make_coupled_page_index({});break;
                case 4:objects[slot]=make_art_index({});break;
                default:objects[slot]=std::make_unique<PageArm>(true);
            }
            objects[slot]->bulk_load(rows);
            builds[slot]=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
            build_positions[slot]=slot;
        };
        for(int position=0;position<6;++position)build((position+offset)%6);

        auto find_arm=[&](int arm,std::string_view key)->std::optional<std::uint64_t> {
            switch(arm) {
                case 0:case 1:return objects[0]->find(key);
                case 2:case 3:return objects[1]->find(key);
                case 4:case 5:return objects[2]->find(key);
                case 6:return objects[5]->find(key);
                case 7:return objects[3]->find(key);
                default:return objects[4]->find(key);
            }
        };
        auto descriptor=[&](int arm)->sib::Descriptor {
            switch(arm) {
                case 0:case 1:return objects[0]->descriptor();
                case 2:case 3:return objects[1]->descriptor();
                case 4:case 5:case 6:return objects[2]->descriptor();
                case 7:return objects[3]->descriptor();
                default:return objects[4]->descriptor();
            }
        };
        // Every row and every mixed query must be answered by every arm.
        std::uint64_t verified=0;
        for(int arm=0;arm<9;++arm) {
            const auto d=descriptor(arm);
            for(const auto& r:rows) {
                if(find_arm(arm,r.key)!=std::optional<sib::Value>(r.value))
                    throw std::logic_error(std::string("row oracle: ")+names[arm]);
                ++verified;
            }
            for(const auto& q:mixed) {
                const auto found=find_arm(arm,q.key);
                if(found.has_value()!=q.expected_present||(found&&*found!=q.expected_value))
                    throw std::logic_error(std::string("query oracle: ")+names[arm]);
                ++verified;
            }
            const auto m=descriptor(arm).capabilities;
            std::cout<<"{\"phase\":\"gate\",\"arm\":\""<<names[arm]<<"\",\"verified\":"<<rows.size()+mixed.size()
                <<",\"implementation\":"<<std::quoted(d.implementation)
                <<",\"implementation_version\":"<<std::quoted(d.implementation_version)
                <<",\"adapter_version\":"<<std::quoted(d.adapter_version)
                <<",\"bulk_load\":"<<std::quoted(m.bulk_load==sib::Support::native?"native":m.bulk_load==sib::Support::adapted?"adapted":"unsupported")
                <<",\"insert\":"<<std::quoted(m.insert==sib::Support::native?"native":m.insert==sib::Support::adapted?"adapted":"unsupported")
                <<",\"scan\":"<<std::quoted(m.scan==sib::Support::native?"native":m.scan==sib::Support::adapted?"adapted":"unsupported")
                <<"}\n";
        }
        std::cout.flush();
        constexpr int williams[]={0,1,8,2,7,3,6,4,5};
        for(std::size_t repeat=0;repeat<6;++repeat) {
            const auto order=permutation(mixed.size(),repeat);
            std::uint64_t checksums[9]={};
            for(int position=0;position<9;++position) {
                const auto arm=(williams[position]+int(repeat)+offset)%9;
                const auto start=Clock::now();
                std::uint64_t checksum=0;
                for(const auto i:order)checksum=checksum*131+token(find_arm(arm,mixed[i].key));
                const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());
                checksums[arm]=checksum;sink=checksum;
                std::cout<<"{\"phase\":\"pass\",\"repeat\":"<<repeat<<",\"position\":"<<position
                    <<",\"arm\":\""<<names[arm]<<"\",\"operations\":"<<mixed.size()
                    <<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum<<"}\n";
            }
            for(int arm=1;arm<9;++arm)if(checksums[arm]!=checksums[0])throw std::logic_error("arm checksum differs");
        }
        std::cout<<"{\"phase\":\"build\",\"build_ns\":{";
        for(int slot=0;slot<6;++slot) {
            if(slot)std::cout<<",";
            std::cout<<"\""<<(slot==0?"native-delta":slot==1?"native-shortdir-first":slot==2?"bm-page-window"
                :slot==3?"F5":slot==4?"ART":"bm-page-base")<<"\":"<<builds[slot];
        }
        std::cout<<"}}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

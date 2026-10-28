#include "coupled_page_adapter.hpp"
#include "static_input.hpp"
#include <iostream>
#include <random>

namespace sib=string_index_bench::v1;
static std::uint64_t checks=0;
static void require(bool condition){++checks;if(!condition)throw std::logic_error("adapter/core/map mismatch");}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("rows.bin required");
        auto stream=static_blocks::buffered(argv[1]);const auto loaded=string_index_bench::unified::read_rows(stream);
        std::vector<coupled_page::Row> rows;std::map<std::string,std::uint64_t,coupled_page::ByteLess> ref;
        for(const auto& r:loaded){rows.push_back({r.key,r.value});ref.emplace(r.key,r.value);}
        require(!rows.empty()&&ref.size()==rows.size());
        coupled_adapter::Adapter adapter;adapter.bulk_load(loaded);coupled_page::Index core(rows);
        auto compare=[&]() {
            checks+=core.verify()+adapter.core_for_diagnostics().verify();
            const auto found=adapter.scan("",SIZE_MAX);const auto native=core.scan("",SIZE_MAX);
            require(found.size()==ref.size()&&native.size()==ref.size());std::size_t i=0;
            for(const auto& r:ref) {
                require(adapter.find(r.first)==r.second&&core.find(r.first)==r.second);
                require(found[i].key==r.first&&found[i].value==r.second&&native[i].key==r.first&&native[i].value==r.second);++i;
            }
        };
        compare();std::mt19937_64 rng(0xf6a20261018ULL);
        for(std::size_t i=0;i<1024;++i) {
            const auto& k=loaded[std::size_t(rng()%loaded.size())].key;const auto v=rng();
            require(adapter.update(k,v)==sib::UpdateResult::updated&&core.update(k,v));ref[k]=v;
        }
        require(adapter.core_for_diagnostics().maintenance().root_publications==1);
        for(std::size_t i=0;i<600;++i) {
            auto k=loaded[std::size_t(rng()%loaded.size())].key;const auto v=i%2?0:UINT64_MAX;
            if(i%3==0)k+="~f6a/"+std::to_string(i);
            if(i%3==0) {
                const bool expected=ref.emplace(k,v).second;
                require((adapter.insert(k,v)==sib::InsertResult::inserted)==expected&&core.insert(k,v)==expected);
                require(adapter.insert(k,7)==sib::InsertResult::already_exists&&!core.insert(k,7));
            }else if(i%3==1) {
                const bool expected=ref.erase(k)!=0;
                require((adapter.erase(k)==sib::EraseResult::erased)==expected&&core.erase(k)==expected);
                require(adapter.update(k,7)==sib::UpdateResult::not_found&&!core.update(k,7));
                require(adapter.insert(k,v)==sib::InsertResult::inserted&&core.insert(k,v));ref.emplace(k,v);
            }else {
                auto at=ref.find(k);const bool expected=at!=ref.end();if(expected)at->second=v;
                require((adapter.update(k,v)==sib::UpdateResult::updated)==expected&&core.update(k,v)==expected);
            }
            const auto at=ref.find(k);const auto expected=at==ref.end()?std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{at->second};
            require(adapter.find(k)==expected&&core.find(k)==expected);
        }
        compare();const auto owned=adapter.scan("",37);adapter.compact();core.compact();compare();
        for(const auto& r:owned)require(adapter.find(r.key)==r.value);
        const auto memory=adapter.memory_usage();
        require(memory.index_owned_bytes==adapter.core_for_diagnostics().memory_bytes());
        require(memory.retained_input_bytes==0&&memory.adapter_owned_bytes==sizeof(adapter));
        std::cout<<"{\"status\":\"passed\",\"rows\":"<<rows.size()<<",\"checks\":"<<checks
            <<",\"initial_updates\":1024,\"mixed_steps\":600,\"mutation_calls\":2224,\"root_publications\":"
            <<adapter.core_for_diagnostics().maintenance().root_publications<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

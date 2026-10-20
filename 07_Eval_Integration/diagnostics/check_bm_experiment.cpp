// R0 corpus gate for the unified experiment factory: all rows, all queries, absent probes,
// boundary bytes, owning scan that outlives the index, and insert/update/erase semantics.
#include "bm_experiment_adapter.hpp"
#include "static_input.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace sib=string_index_bench::v1;
namespace uni=string_index_bench::unified;

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows.bin queries.bin lookup");
        const std::string lookup=argv[3];
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);const auto queries=uni::read_queries(qs);
        if(rows.empty()||queries.empty())throw std::invalid_argument("empty workload");
        std::map<std::string,std::uint64_t> reference;
        for(const auto& r:rows)reference.emplace(r.key,r.value);

        std::uint64_t checks=0,mismatch=0;
        auto require=[&](bool ok,const char* what) {++checks;if(!ok){++mismatch;std::cerr<<what<<'\n';}};

        auto index=make_bm_experiment_index({{"lookup",lookup}});
        require(index->descriptor().capabilities.find==sib::Support::native,"find capability");
        index->bulk_load(rows);
        require(index->memory_usage().index_owned_bytes.value_or(0)>0,"memory reported");

        for(const auto& r:rows)require(index->find(r.key)==std::optional<sib::Value>(r.value),"row oracle");
        for(const auto& q:queries) {
            const auto found=index->find(q.key);
            require(found.has_value()==q.expected_present,"query presence");
            if(q.expected_present)require(found&&*found==q.expected_value,"query value");
        }
        // Absent and boundary probes against the map.
        std::vector<std::string> probes{"",std::string(1,'\0'),std::string(1,char(255)),
            std::string("a\0b",3),std::string(4096,'z')};
        for(std::size_t i=0;i<rows.size();i+=997)probes.push_back(rows[i].key+"~absent");
        for(const auto& probe:probes) {
            const auto want=reference.find(probe);
            const auto got=index->find(probe);
            require(got.has_value()==(want!=reference.end()),"absent presence");
            if(want!=reference.end())require(got&&*got==want->second,"absent value");
        }
        // Owning scan: keep the rows, destroy the index, then verify them.
        auto scanned=index->scan("",SIZE_MAX);
        require(scanned.size()==rows.size(),"full scan size");
        std::size_t i=0;
        for(const auto& r:rows)require(scanned[i].key==r.key&&scanned[i].value==r.value,"scan row"),++i;
        auto head=index->scan(rows[rows.size()/2].key,17);
        require(head.size()==17,"bounded scan size");
        index.reset();
        require(scanned.size()==rows.size()&&scanned.front().key==rows.front().key,"scan survives destroy");

        // Insert / update / erase against the map, on a fresh instance.
        auto second=make_bm_experiment_index({{"lookup",lookup}});
        second->bulk_load(rows);
        std::mt19937_64 rng(20261020);
        std::uint64_t inserted=0,updated=0,erased=0;
        for(int n=0;n<200;++n) {
            const auto base=rows[rng()%rows.size()].key;
            std::string key=base+"~r0/"+std::to_string(n);
            require(second->insert(key,1'000+n)==sib::InsertResult::inserted,"insert new");
            require(reference.emplace(key,1'000+n).second,"map insert");
            require(second->find(key)==std::optional<sib::Value>(1'000+n),"insert read-back");
            ++inserted;
        }
        for(int n=0;n<100;++n) {
            const auto& key=rows[rng()%rows.size()].key;
            require(second->update(key,2'000+n)==sib::UpdateResult::updated,"update existing");
            reference[key]=2'000+n;
            require(second->find(key)==std::optional<sib::Value>(2'000+n),"update read-back");
            ++updated;
        }
        for(int n=0;n<100;++n) {
            const std::string key=rows[rng()%rows.size()].key;
            const bool present=reference.find(key)!=reference.end();
            require(second->erase(key)==(present?sib::EraseResult::erased:sib::EraseResult::not_found),"erase result");
            if(present)reference.erase(key);
            ++erased;
        }
        require(second->find(std::string(1,'\0'))==std::nullopt,"absent after erase");

        const auto m=second->memory_usage();
        std::cout<<"{\"lookup\":\""<<lookup<<"\",\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()
            <<",\"checks\":"<<checks<<",\"mismatches\":"<<mismatch
            <<",\"inserted\":"<<inserted<<",\"updated\":"<<updated<<",\"erased\":"<<erased
            <<",\"index_bytes\":"; (m.index_owned_bytes?std::cout<<*m.index_owned_bytes:std::cout<<"null");
        std::cout<<"}\n";
        return mismatch?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Run exactly the shared interface suite, then corpus/scan/work gates for three native factories.
#include "benchmark/factories.h"
static string_index_bench::v1::Factory cost_selected_factory=nullptr;
static std::unique_ptr<string_index_bench::v1::Index> make_cost_selected(
    const string_index_bench::v1::Parameters& p){return cost_selected_factory(p);}
#define ACCEPTANCE_FACTORY_HEADER "benchmark/factories.h"
#define ACCEPTANCE_FACTORY_SYMBOL make_cost_selected
#define main cost_shared_acceptance_main
#include "borrowed_contract_acceptance.cpp"
#undef main
#include "learned_index.h"
#include "static_input.hpp"
#include <iomanip>

namespace uni=string_index_bench::unified;
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows queries");
        const sib::Factory factories[]={make_block_residual_delta_shortdir_first,
            make_block_residual_delta_shortdir_first_cost,make_block_adaptive_delta_shortdir_first};
        const char* names[]={"legacy","cost","adaptive"};
        auto rs=static_blocks::buffered(argv[1]),qs=static_blocks::buffered(argv[2]);
        const auto rows=uni::read_rows(rs);auto queries=uni::read_queries(qs);
        std::map<std::string,std::uint64_t> oracle;for(const auto& r:rows)oracle.emplace(r.key,r.value);
        for(std::size_t i=0;i<queries.size();++i)if(i%10==9) {
            queries[i].key+="~f2/miss";while(oracle.count(queries[i].key))queries[i].key+="~";
        }
        std::ostringstream records;
        records<<"{\"rows\":"<<rows.size()<<",\"queries\":"<<queries.size()<<",\"arms\":[";
        for(int arm=0;arm<3;++arm) {
            cost_selected_factory=factories[arm];const auto initial_checks=checks;
            if(cost_shared_acceptance_main())throw std::logic_error("common acceptance failed");
            const auto accepted=checks-initial_checks;
            auto index=factories[arm]({{"instrument","1"}});index->bulk_load(rows);
            auto& own=dynamic_cast<blocklearn::LearnedIndex&>(*index);own.validate();
            std::uint64_t semantic=0,scan_rows=0;
            auto check_point=[&](std::string_view k) {
                auto it=oracle.find(std::string(k));
                const auto expected=it==oracle.end()?std::optional<std::uint64_t>{}:it->second;
                if(index->find(k)!=expected)throw std::logic_error("corpus point oracle");++semantic;
            };
            auto check_scan=[&](std::string_view k,std::size_t n) {
                auto out=index->scan(k,n);auto it=oracle.lower_bound(std::string(k));
                for(const auto& row:out) {
                    if(it==oracle.end()||row.key!=it->first||row.value!=it->second)throw std::logic_error("scan oracle");
                    ++it;++scan_rows;
                }
                if(out.size()>n||(out.size()<n&&it!=oracle.end()))throw std::logic_error("scan length");++semantic;
            };
            for(const auto& r:rows)check_point(r.key);
            for(const auto& q:queries)check_point(q.key);
            check_scan("",rows.size()+1);
            for(std::size_t i=0;i<rows.size();i+=257) {
                check_point(rows[i].key+std::string(1,'\0'));
                if(!rows[i].key.empty())check_point(rows[i].key.substr(0,rows[i].key.size()-1));
                check_scan(rows[i].key,17);
            }
            const auto before=own.statistics();
            for(const auto& q:queries)index->find(q.key);
            const auto after=own.statistics();const auto mem=index->memory_usage();
            own.validate();
            if(arm)records<<',';
            records<<"{\"arm\":\""<<names[arm]<<"\",\"acceptance_checks\":"<<accepted
                <<",\"semantic_checks\":"<<semantic<<",\"scan_rows\":"<<scan_rows
                <<",\"queries_counted\":"<<after.queries-before.queries
                <<",\"tag_batches\":"<<after.tag_batches-before.tag_batches
                <<",\"full_key_comparisons\":"<<after.full_key_comparisons-before.full_key_comparisons
                <<",\"tag_tests\":"<<after.tag_tests-before.tag_tests
                <<",\"directory_fallbacks\":"<<after.directory_fallbacks-before.directory_fallbacks
                <<",\"leaves\":"<<after.leaves<<",\"hash_leaves\":"<<after.hash_leaves
                <<",\"residual_leaves\":"<<after.residual_leaves
                <<",\"predicted_first_leaves\":"<<after.predicted_first_leaves
                <<",\"owned_bytes\":"<<mem.index_owned_bytes.value()
                <<",\"retained_input_bytes\":"<<mem.retained_input_bytes.value()<<"}";
        }
        std::cout<<records.str()<<"]}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

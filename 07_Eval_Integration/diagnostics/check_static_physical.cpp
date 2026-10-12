#include "static_physical_blocks.hpp"
#include "unified/runner.h"
#include "switches.hpp"
#include "static_input.hpp"
#include <fstream>
#include <iostream>
#include <random>

namespace unified=string_index_bench::unified;
using static_blocks::Row;
static std::uint64_t checked=0;
static std::optional<std::uint64_t> oracle(const std::vector<Row>& rows,std::string_view k) {
    const auto it=std::lower_bound(rows.begin(),rows.end(),k,[](const Row& r,std::string_view q){return validation::compare(r.key,q)<0;});
    return it!=rows.end()&&validation::compare(it->key,k)==0?std::optional<std::uint64_t>(it->value):std::nullopt;
}
static void check(const static_blocks::Index& index,const std::vector<Row>& rows,std::string_view k) {
    index.check_window(k);
    const auto expected=oracle(rows,k);
    if(index.find_model(k)!=expected||index.find_directory(k)!=expected)
        throw std::logic_error("owning static lookup disagrees with independent oracle");
    ++checked;
}
static void domain_gate() {
    for(const auto& rows:std::vector<std::vector<Row>>{{},{{"",0}},{{"singleton",UINT64_MAX}}}) {
        static_blocks::Index index(rows);for(const auto& k:std::vector<std::string>{"","a","singleton",std::string(1,'\0')})check(index,rows,k);
    }
    std::vector<Row> rows{{"",0},{"a",1},{std::string("a\0",2),2},{std::string(1,char(128)),3},
                          {std::string(1,char(255)),4},{std::string(4096,'z'),UINT64_MAX}};
    for(unsigned i=0;i<2000;++i) {
        std::string k="ambiguous/"+std::string(64,'x');k.push_back(char(i>>8));k.push_back(char(i));
        rows.push_back({std::move(k),i+10});
    }
    for(unsigned i=0;i<300;++i) {
        std::string k;k.push_back(char((i*193)%256));k.push_back(char((i*71)%256));k.push_back(char(i>>8));
        rows.push_back({std::move(k),i+3000});
    }
    std::sort(rows.begin(),rows.end(),[](const Row& a,const Row& b){return validation::compare(a.key,b.key)<0;});
    rows.erase(std::unique(rows.begin(),rows.end(),[](const Row& a,const Row& b){return a.key==b.key;}),rows.end());
    for(auto chunk:{2048u,64u}) {
        auto input=rows;static_blocks::Index index(input,chunk);
        std::vector<Row>().swap(input); // query after source lifetime ends
        checked+=index.verify_numeric_certificate();
        for(const auto& row:rows) {
            check(index,rows,row.key);check(index,rows,row.key+std::string(1,'\0'));
            check(index,rows,row.key+std::string(1,char(255)));
            if(!row.key.empty())check(index,rows,std::string_view(row.key).substr(0,row.key.size()-1));
        }
        for(unsigned i=0;i<65536;++i) {
            std::string key;key.push_back(char(i>>8));key.push_back(char(i));check(index,rows,key);
        }
        // Query all valid symbol neighbours around real block coordinates.
        // The gap proof in the model also covers numeric coordinates that encode no string.
        for(unsigned i=0;i<256;++i)check(index,rows,std::string(1,char(i)));
    }
}
static std::size_t percentile(std::vector<std::size_t> xs,double p) {
    if(xs.empty())return 0;std::sort(xs.begin(),xs.end());return xs[static_cast<std::size_t>((xs.size()-1)*p)];
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::invalid_argument("rows.bin queries.bin");
        domain_gate();const auto domain=checked;
        auto row_stream=static_blocks::buffered(argv[1]),query_stream=static_blocks::buffered(argv[2]);
        auto rows=unified::read_rows(row_stream);const auto queries=unified::read_queries(query_stream);
        auto input=rows;static_blocks::Index index(input);std::vector<Row>().swap(input);
        const auto certificate_checks=index.verify_numeric_certificate();checked+=certificate_checks;
        for(const auto& row:rows)check(index,rows,row.key);
        for(const auto& q:queries) {
            check(index,rows,q.key);const auto expected=oracle(rows,q.key);
            if(expected.has_value()!=q.expected_present||(expected&&*expected!=q.expected_value))throw std::logic_error("interchange oracle");
        }
        // Every block boundary plus short-prefix and binary neighbours (including absent keys).
        for(std::size_t i=0;i<rows.size();i+=16) {
            const auto& k=rows[i].key;check(index,rows,k+std::string(1,'\0'));check(index,rows,k+std::string(1,char(255)));
            if(!k.empty())check(index,rows,std::string_view(k).substr(0,k.size()-1));
        }
        static_blocks::Work work;
        std::uint64_t baseline_blocks=0,baseline_compares=0;
        auto cfg=e25sw::baseline_config(128);e25sw::pin_frozen_switches(cfg);cfg.compact_region_routing=true;
        validation::Index baseline(cfg);std::vector<validation::Pair> pairs;
        for(const auto& r:rows)pairs.emplace_back(r.key,r.value);baseline.bulk_load(pairs);std::vector<validation::Pair>().swap(pairs);
        validation::Mode mode;mode.route=validation::Route::Interval;mode.prefetch=validation::Prefetch::Off;
        mode.fingerprint=true;mode.simd=true;mode.ov=validation::OvSearch::Hybrid;mode.ov_thr=128;mode.locate=false;
        for(const auto& q:queries) {
            index.find_model(q.key,&work);const auto old=baseline.find(q.key,mode);
            if(old.found!=q.expected_present||(old.found&&old.value!=q.expected_value))throw std::logic_error("current C baseline mismatch");
            baseline_blocks+=old.subblocks_visited;baseline_compares+=old.str_cmps_primary;
        }
        const auto m=index.memory();
        std::cout<<"{\"checks\":"<<checked<<",\"domain_checks\":"<<domain<<",\"rows\":"<<rows.size()
                 <<",\"certificate_checks\":"<<certificate_checks
                 <<",\"queries\":"<<work.queries<<",\"prefix_rejected\":"<<work.rejected
                 <<",\"segments\":"<<index.segment_count()<<",\"physical_blocks\":"<<index.block_count()
                 <<",\"baseline_bytes\":"<<baseline.memory()<<",\"baseline_blocks\":"<<baseline_blocks
                 <<",\"baseline_comparisons\":"<<baseline_compares<<",\"candidate_blocks\":"<<work.blocks
                 <<",\"candidate_comparisons\":"<<work.exact<<",\"model_search_steps\":"<<work.model_steps
                 <<",\"wide_windows\":"<<work.wide<<",\"windows_total\":"<<work.window_blocks
                 <<",\"window_p50\":"<<percentile(work.windows,.50)<<",\"window_p90\":"<<percentile(work.windows,.90)
                 <<",\"window_p99\":"<<percentile(work.windows,.99)<<",\"window_max\":"<<percentile(work.windows,1)
                 <<",\"entered_p50\":"<<percentile(work.entered,.50)<<",\"entered_p99\":"<<percentile(work.entered,.99)
                 <<",\"memory\":{\"total\":"<<m.total()<<",\"object\":"<<m.object<<",\"arena\":"<<m.arena
                 <<",\"blocks\":"<<m.blocks<<",\"regions\":"<<m.regions<<",\"prefixes\":"<<m.prefixes
                 <<",\"fences\":"<<m.fences<<",\"model\":"<<m.model<<",\"routing\":"<<m.routing<<"}}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// E6-9 diagnostic (separate run, never part of timing): how many integer and full
// string comparisons do A/B actually execute per query on real data?
// Built with -DE6_ROUTE_COUNTERS.
#include "data.hpp"
#include "route_aux.hpp"
#include <algorithm>
#include <iostream>
#include <random>
using namespace validation;
int main(int argc,char**argv){try{
    if(argc<2)throw std::invalid_argument("diag_route KIND N [TRANCOCVS] [QUERIES]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]);
    const std::string csv=argc>3?argv[3]:"";
    const std::size_t nq=argc>4?std::stoull(argv[4]):4096;
    const auto keys=make_data(kind,n,csv);
    Config c;c.block_bytes=1024;c.slots=16;c.model_kind=ModelKind::Context16;c.region_keys=2048;
    Index idx(c);idx.bulk_load(keys);
    RouterA ra;RouterB rb;ra.sync(idx);rb.sync(idx);
    const auto da=ra.diag();
    std::mt19937_64 rng(2026091310);
    std::uint64_t a_int=0,a_str=0,b_int=0,b_str=0;
    for(std::size_t i=0;i<nq;++i){
        std::string k=keys[rng()%keys.size()].first;
        if(i%2)k+="~missing~";
        const std::uint64_t ai0=ra.int_cmps,as0=ra.str_cmps,bi0=rb.int_cmps,bs0=rb.str_cmps;
        ra.locate(idx,k);rb.locate(idx,k);
        a_int+=ra.int_cmps-ai0;a_str+=ra.str_cmps-as0;
        b_int+=rb.int_cmps-bi0;b_str+=rb.str_cmps-bs0;
    }
    std::size_t m=idx.region_count(),cmps=0;      // std-style upper_bound comparison count
    for(std::size_t t=m+1;t>1;t>>=1)++cmps;       // ceil(log2(m+1))
    std::cout<<"{\"kind\":\""<<kind<<"\",\"fences\":"<<da.fences
        <<",\"distinct_u\":"<<da.distinct_u<<",\"equal_share\":"<<da.equal_share
        <<",\"max_equal_run\":"<<da.max_equal_run
        <<",\"queries\":"<<nq
        <<",\"A_avg_int_cmps\":"<<(double)a_int/nq
        <<",\"A_avg_str_cmps\":"<<(double)a_str/nq
        <<",\"B_avg_int_cmps\":"<<(double)b_int/nq
        <<",\"B_avg_str_cmps\":"<<(double)b_str/nq
        <<",\"baseline_avg_str_cmps\":"<<cmps
        <<"}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<"\n";return 1;}}

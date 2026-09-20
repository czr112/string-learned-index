// E7-5 diagnostic (separate run, -DE7_COUNT_BYTES): per-class cost breakdown for
// the three overflow lookup policies. Classes: primary hit / overflow hit / miss.
// Per class: lookups, full string compares, record touches, bytes walked by the
// compares, and the overflow-enter rate (structural: every non-primary-hit query
// enters the overflow structure; counted here explicitly, not derived).
// Reads the frozen hybrid threshold from frozen_ov_<kind>.json.
#include "data.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace validation;

int main(int argc,char**argv){try{
#ifdef _WIN32
    SetProcessAffinityMask(GetCurrentProcess(),1);
#endif
    if(argc<2)throw std::invalid_argument("diag_ov KIND N [TRANCOCVS]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]);
    const std::string csv=argc>3?argv[3]:"";
    const auto keys=make_data(kind,n,csv);
    std::uint32_t thr=32;
    {std::ifstream fz("results/main/frozen_ov_"+kind+".json");std::stringstream ss;ss<<fz.rdbuf();
     const std::string t=ss.str();const auto p=t.find("\"frozen_hybrid_threshold\":");
     if(p==std::string::npos)throw std::runtime_error("frozen file missing");
     thr=static_cast<std::uint32_t>(std::stoul(t.substr(p+26)));}
    Config c;c.block_bytes=1024;c.slots=16;c.model_kind=ModelKind::Context16;c.region_keys=2048;
    Index idx(c);idx.bulk_load(keys);
    const Mode modes[3]{Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Linear,32},
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Binary,32},
        Mode{Route::Interval,Prefetch::Off,true,true,OvSearch::Hybrid,thr}};
    const char*names[3]{"linear","binary","hybrid"};
    std::mt19937_64 rng(2026091390);
    // test-identity only (rank%4 != 0), matching the E7 test phase
    std::vector<std::size_t> test_e;for(std::size_t i=0;i<keys.size();++i)if(i%4)test_e.push_back(i);
    const std::size_t nq=8192;
    std::cout<<"{\"kind\":\""<<kind<<"\",\"frozen_threshold\":"<<thr<<",\"queries_per_class_sample\":"<<nq<<",\"policies\":[";
    for(int pi=0;pi<3;++pi){
        if(pi)std::cout<<',';
        // per-class accumulators: 0 primary hit, 1 overflow hit, 2 miss
        std::uint64_t cnt[3]={0,0,0},strs[3]={0,0,0},touch[3]={0,0,0},bytes[3]={0,0,0},entered=0;
        for(std::size_t i=0;i<nq;++i){
            const auto&kv=keys[test_e[rng()%test_e.size()]];
            std::string k=kv.first;const bool hit=(i%2)==0;
            if(!hit)k+="~missing~";
            const auto r=idx.find(k,modes[pi]);
            int cls=r.source==1?0:r.source==2?1:2;
            if(r.source!=1)++entered;
            ++cnt[cls];strs[cls]+=r.str_cmps;touch[cls]+=r.overflow_checks;bytes[cls]+=r.cmp_bytes;
        }
        std::cout<<"{\"name\":\""<<names[pi]<<"\",\"overflow_enter_rate\":"
            <<(double)entered/nq<<",\"classes\":[";
        for(int cl=0;cl<3;++cl){
            if(cl)std::cout<<',';
            std::cout<<"{\"class\":"<<(cl==0?"\"primary_hit\"":cl==1?"\"overflow_hit\"":"\"miss\"")
                <<",\"lookups\":"<<cnt[cl]
                <<",\"avg_str_cmps\":"<<(cnt[cl]?(double)strs[cl]/cnt[cl]:0.0)
                <<",\"avg_record_touches\":"<<(cnt[cl]?(double)touch[cl]/cnt[cl]:0.0)
                <<",\"avg_cmp_bytes\":"<<(cnt[cl]?(double)bytes[cl]/cnt[cl]:0.0)<<"}";
        }
        std::cout<<"]}";
    }
    std::cout<<"]}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<"\n";return 1;}}

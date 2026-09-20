// E8-a diagnostic: per-group capacity vs demand concentration. Question (review
// 2026-09-13): does overflow come from insufficient total capacity, or from
// uneven allocation across groups with a fixed model and fixed group mapping?
#include "data.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
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
    if(argc<2)throw std::invalid_argument("diag_capacity KIND N [TRANCOCVS] [BLOCK] [SLOTS]");
    const std::string kind=argv[1];const std::size_t n=std::stoull(argv[2]);
    const std::string csv=argc>3?argv[3]:"";
    const std::uint32_t B=argc>4?static_cast<std::uint32_t>(std::stoul(argv[4])):1024;
    const std::uint32_t S=argc>5?static_cast<std::uint32_t>(std::stoul(argv[5])):16;
    const auto keys=make_data(kind,n,csv);
    Config c;c.block_bytes=B;c.slots=S;c.model_kind=ModelKind::Context16;c.region_keys=2048;
    Index idx(c);idx.bulk_load(keys);
    // gather per-group demand across all regions
    std::vector<std::uint64_t> ov_entries,ov_bytes,prim_entries;
    std::uint64_t total_keys=0,total_overflow=0,groups=0;
    for(std::size_t i=0;i<idx.region_count();++i){
        const auto&r=idx.region(i);
        for(const auto&g:r.group_stats()){
            ++groups;total_keys+=g.primary_entries+g.overflow_entries;total_overflow+=g.overflow_entries;
            ov_entries.push_back(g.overflow_entries);ov_bytes.push_back(g.overflow_bytes);
            prim_entries.push_back(g.primary_entries);
        }
    }
    auto top_share=[](std::vector<std::uint64_t> v,double frac){
        std::sort(v.begin(),v.end(),std::greater<>());
        std::uint64_t tot=0;for(auto x:v)tot+=x;
        std::uint64_t acc=0;const std::size_t k=std::max<std::size_t>(1,std::size_t(v.size()*frac));
        for(std::size_t i=0;i<k;++i)acc+=v[i];
        return tot?double(acc)/double(tot):0.0;};
    auto gini=[](std::vector<std::uint64_t> v){
        std::sort(v.begin(),v.end());std::uint64_t tot=0;for(auto x:v)tot+=x;
        if(!tot)return 0.0;double d=0;std::uint64_t acc=0;std::size_t m=v.size();
        for(std::size_t i=0;i<m;++i){acc+=v[i];d+=double(2*(i+1)-m-1)*double(v[i]);}
        return m?d/(double(m)*double(tot)):0.0;};
    std::uint64_t empty_groups=0,capped_groups=0;
    for(auto e:prim_entries)if(e==0)++empty_groups;
    for(auto e:ov_entries)if(e>0)++capped_groups;
    const std::uint64_t cap_per_group=std::uint64_t(S);
    std::uint64_t slot_ok=0; // overflow records that would fit an empty primary block of this layout
    // overflow length stats need region access; approximate via overflow_bytes>0 counts
    std::cout<<"{\"kind\":\""<<kind<<"\",\"block\":"<<B<<",\"slots\":"<<S
        <<",\"keys\":"<<total_keys<<",\"groups\":"<<groups
        <<",\"overflow_keys\":"<<total_overflow
        <<",\"overflow_share\":"<<(total_keys?double(total_overflow)/total_keys:0.0)
        <<",\"groups_with_overflow\":"<<capped_groups
        <<",\"empty_groups\":"<<empty_groups
        <<",\"overflow_entries_gini\":"<<gini(ov_entries)
        <<",\"overflow_top10pct_share\":"<<top_share(ov_entries,0.10)
        <<",\"overflow_top25pct_share\":"<<top_share(ov_entries,0.25)
        <<",\"primary_entries_gini\":"<<gini(prim_entries)
        <<"}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<"\n";return 1;}}

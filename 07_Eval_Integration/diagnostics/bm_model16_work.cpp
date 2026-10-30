// One fixed model-budget intervention: same encoded physical layout and write policy.
#define main bm_model16_legacy_entry
#include "bm_tx_query.cpp"
#undef main
#include BM_MODEL16_INDEX_HEADER
#include "bm_followup_memory.hpp"
#include "bm_core_trace_oracle.hpp"

namespace mem=bm_followup_memory;
static_assert(sizeof(bm_tx_page::Index)==sizeof(bm_m16_tx::Index),"model-only Index layout");
static_assert(sizeof(bm_n1_leaf::Page)==sizeof(bm_m16_leaf::Page),"model-only Page layout");
struct Shape {
    std::string lower;std::size_t rows,blocks;std::vector<std::string> fences;std::uint64_t payload;
    bool operator==(const Shape& other)const {
        return lower==other.lower&&rows==other.rows&&blocks==other.blocks&&fences==other.fences&&payload==other.payload;
    }
};
struct Counts {
    std::string group;
    std::uint64_t queries,rejected,model_calls,proposed_blocks,boundary_checks,outside,fallbacks,directory_steps,payload_blocks,exact;
};
struct Profile {
    std::string arm,model;std::size_t terminal,build_peak,maintenance_peak,pages,blocks,segments,training_certified;
    std::uint64_t probe_count=0,find_allocations=0;std::vector<Shape> shape;std::vector<Counts> groups;
};
template<class Index,class Work> static Profile collect(const char* arm,const Case& test,
                                                       const bm_core_oracle::Trace& oracle,bool dynamic) {
    Profile result;mem::begin();auto index=std::make_unique<Index>(dynamic?test.initial:test.terminal);
    result.build_peak=mem::peak;mem::peak=mem::live;
    if(dynamic)oracle.replay(*index,test.operations);
    result.terminal=mem::live;result.maintenance_peak=mem::peak;
    // From here, diagnostic result buffers are explicitly excluded from index bytes.
    mem::tracking=false;result.arm=arm;index->verify();
    {
        const auto rows=index->scan("",SIZE_MAX);need(rows.size()==test.terminal.size(),"model terminal size");
        for(std::size_t i=0;i<rows.size();++i)
            need(rows[i].key==test.terminal[i].key&&rows[i].value==test.terminal[i].value,"model terminal rows");
    }
    result.pages=index->page_count();result.segments=index->bm_segment_count();result.training_certified=index->bm_certified_pages();result.blocks=0;
    for(const auto& binding:index->root_identity_for_testing()->bindings()) {
        const auto& page=*binding.page;result.blocks+=page.block_count();
        result.shape.push_back({binding.lower,page.size(),page.block_count(),page.fence_keys(),page.payload_identity()});
        result.model=page.build_identity().model;
    }
    mem::arm(0);
    for(const auto& group:test.query.groups)for(const auto& query:group.queries) {
        need(index->find_base(query.key)==(query.hit?std::optional<sib::Value>(query.value):std::nullopt),"model zero-allocation answer");
        ++result.probe_count;
    }
    result.find_allocations=mem::disarm();need(result.find_allocations==0,"model find allocated");
    for(const auto& group:test.query.groups) {
        Work work;
        for(const auto& query:group.queries)
            need(index->find_base(query.key,&work)==(query.hit?std::optional<sib::Value>(query.value):std::nullopt),"model counted answer");
        result.groups.push_back({group.name,work.queries,work.rejected,work.bm_calls,work.bm_proposal_blocks,
             work.bm_boundary_compares,work.bm_outside,work.fallback,work.directory_steps,work.blocks,work.exact});
    }
    index.reset();need(mem::live==0,"model destruction bytes");return result;
}
static void emit(const Profile& p) {
    std::cout<<"{\"arm\":\""<<p.arm<<"\",\"model\":\""<<p.model<<"\",\"terminal\":"<<p.terminal
             <<",\"build_peak\":"<<p.build_peak<<",\"maintenance_peak\":"<<p.maintenance_peak<<",\"pages\":"<<p.pages
             <<",\"blocks\":"<<p.blocks<<",\"segments\":"<<p.segments<<",\"training_certified_pages\":"<<p.training_certified
             <<",\"probe_count\":"<<p.probe_count<<",\"find_allocations\":"<<p.find_allocations
             <<",\"shape_and_payload_signatures_match\":true,\"groups\":[";
    bool first=true;
    for(const auto& g:p.groups) {
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"group\":\""<<g.group<<"\",\"queries\":"<<g.queries<<",\"prefix_rejects\":"<<g.rejected
                 <<",\"model_calls\":"<<g.model_calls<<",\"proposed_blocks\":"<<g.proposed_blocks<<",\"boundary_checks\":"<<g.boundary_checks
                 <<",\"model_outside\":"<<g.outside<<",\"fallbacks\":"<<g.fallbacks<<",\"directory_steps\":"<<g.directory_steps
                 <<",\"payload_blocks\":"<<g.payload_blocks<<",\"exact_compares\":"<<g.exact<<'}';
    }
    std::cout<<"]}\n";
}
int main(int argc,char** argv) {
    try {
        need(argc==7&&std::string(argv[1])=="profile","profile rows ops pool final queries");
        Case test(argv,0);bm_core_oracle::Trace oracle(test.initial,test.operations,test.terminal);
        for(const bool dynamic:{true,false}) {
            const auto baseline=collect<bm_tx_page::Index,bm_n1_leaf::Work>(dynamic?"tx8_dynamic":"tx8_snapshot",test,oracle,dynamic);
            const auto candidate=collect<bm_m16_tx::Index,bm_m16_leaf::Work>(dynamic?"m16_dynamic":"m16_snapshot",test,oracle,dynamic);
            need(baseline.shape==candidate.shape,"model physical shape/fence/payload signature drift");
            need(baseline.model=="BlockModel-fence-1-7-8-1"&&candidate.model=="BlockModel-fence-1-7-16-1","model identity");
            emit(baseline);emit(candidate);
        }
    }catch(const std::exception& error){mem::disarm();std::cerr<<error.what()<<'\n';return 1;}
}

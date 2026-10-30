// Fixed8, same encoded layout; refit only at a diagnostic terminal checkpoint.
#define main bm_refit_legacy_entry
#include "bm_tx_query.cpp"
#undef main
#include BM_REFIT_INDEX_HEADER
#include "bm_followup_memory.hpp"
#include "bm_core_trace_oracle.hpp"

namespace mem=bm_followup_memory;
static_assert(sizeof(bm_tx_page::Index)==sizeof(bm_refit_tx::Index),"refit Index layout");
static_assert(sizeof(bm_n1_leaf::Page)==sizeof(bm_refit_leaf::Page),"refit Page layout");
struct Layout {
    std::string lower;std::size_t rows,blocks;std::vector<std::string> fences;std::uint64_t payload;
    bool operator==(const Layout& p)const {
        return lower==p.lower&&rows==p.rows&&blocks==p.blocks&&fences==p.fences&&payload==p.payload;
    }
};
template<class Index> static std::vector<Layout> layout(const Index& index) {
    std::vector<Layout> result;
    for(const auto& binding:index.root_identity_for_testing()->bindings()) {
        const auto& p=*binding.page;
        result.push_back({binding.lower,p.size(),p.block_count(),p.fence_keys(),p.payload_identity()});
        need(p.build_identity().model=="BlockModel-fence-1-7-8-1","fixed8 model identity");
    }
    return result;
}
template<class Index> static void terminal_check(const Index& index,const Case& test) {
    index.verify();const auto rows=index.scan("",SIZE_MAX);need(rows.size()==test.terminal.size(),"refit terminal cardinality");
    for(std::size_t i=0;i<rows.size();++i)
        need(rows[i].key==test.terminal[i].key&&rows[i].value==test.terminal[i].value,"refit terminal values");
}
template<class Index,class Work> static void emit(const char* arm,const Index& index,const Case& test,
                                                std::size_t terminal,std::size_t refit_peak,std::size_t training) {
    terminal_check(index,test);std::uint64_t queries=0;mem::arm(0);
    for(const auto& group:test.query.groups)for(const auto& q:group.queries) {
        need(index.find_base(q.key)==(q.hit?std::optional<sib::Value>(q.value):std::nullopt),"refit zero-allocation values");++queries;
    }
    need(mem::disarm()==0,"refit find allocation");
    std::cout<<"{\"arm\":\""<<arm<<"\",\"terminal\":"<<terminal<<",\"refit_peak\":"<<refit_peak
             <<",\"training_fences\":"<<training<<",\"pages\":"<<index.page_count()<<",\"segments\":"<<index.bm_segment_count()
             <<",\"queries\":"<<queries<<",\"find_allocations\":0,\"groups\":[";bool first=true;
    for(const auto& group:test.query.groups) {
        if(!first)std::cout<<',';first=false;Work w;
        for(const auto& q:group.queries)
            need(index.find_base(q.key,&w)==(q.hit?std::optional<sib::Value>(q.value):std::nullopt),"refit counted values");
        std::cout<<"{\"group\":\""<<group.name<<"\",\"queries\":"<<w.queries<<",\"prefix_rejects\":"<<w.rejected
                 <<",\"model_calls\":"<<w.bm_calls<<",\"proposed_blocks\":"<<w.bm_proposal_blocks
                 <<",\"boundary_checks\":"<<w.bm_boundary_compares<<",\"model_outside\":"<<w.bm_outside
                 <<",\"fallbacks\":"<<w.fallback<<",\"directory_steps\":"<<w.directory_steps
                 <<",\"payload_blocks\":"<<w.blocks<<",\"exact_compares\":"<<w.exact<<'}';
    }
    std::cout<<"]}\n";
}
int main(int argc,char** argv) {
    try {
        need(argc==7&&std::string(argv[1])=="profile","profile rows ops pool final queries");
        Case test(argv,0);bm_core_oracle::Trace oracle(test.initial,test.operations,test.terminal);
        std::vector<Layout> original;
        {
            mem::begin();auto index=std::make_unique<bm_tx_page::Index>(test.initial);oracle.replay(*index,test.operations);
            const auto terminal=mem::live;mem::tracking=false;original=layout(*index);
            emit<bm_tx_page::Index,bm_n1_leaf::Work>("original8",*index,test,terminal,terminal,0);
            index.reset();need(mem::live==0,"original refit diagnostic leak");
        }
        {
            mem::begin();auto index=std::make_unique<bm_refit_tx::Index>(test.initial);oracle.replay(*index,test.operations);
            const auto terminal=mem::live;mem::tracking=false;const auto before=layout(*index);
            need(before==original,"isolated refit copy physical signatures");
            emit<bm_refit_tx::Index,bm_refit_leaf::Work>("copy8_before",*index,test,terminal,terminal,0);
            const auto* root=index->root_identity_for_testing();mem::peak=mem::live;mem::tracking=true;
            const auto training=index->refit_models_for_diagnostics();const auto after_bytes=mem::live,peak=mem::peak;
            mem::tracking=false;need(index->root_identity_for_testing()==root,"refit root changed");
            need(layout(*index)==before,"refit changed physical payload/fences");
            emit<bm_refit_tx::Index,bm_refit_leaf::Work>("copy8_refit",*index,test,after_bytes,peak,training);
            index.reset();need(mem::live==0,"refit diagnostic destruction leak");
        }
    }catch(const std::exception& error){mem::disarm();mem::tracking=false;std::cerr<<error.what()<<'\n';return 1;}
}

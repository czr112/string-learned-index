// Deterministic work counters, with public-factory semantic validation first.
#define main bm_read_work_legacy_entry
#include "bm_tx_query.cpp"
#undef main

template<class Index> static void replay_mutations(Index& index,const std::vector<Op>& operations) {
    for(const auto& op:operations) {
        switch(op.kind) {
        case 2:index.insert(op.key,op.value);break;
        case 3:index.update(op.key,op.value);break;
        case 4:index.erase(op.key);break;
        default:break; // find/scan do not mutate the validated concrete Index.
        }
    }
}
static std::size_t percentile(std::vector<std::size_t> values,std::size_t numerator) {
    need(!values.empty(),"nonempty percentile");std::sort(values.begin(),values.end());
    const auto rank=(values.size()*numerator+99)/100;
    return values[rank?rank-1:0];
}
template<class Index> static void profile(const char* name,Index& index,const Case& test) {
    index.verify();const auto terminal=index.scan("",SIZE_MAX);
    need(terminal.size()==test.terminal.size(),"concrete terminal size");
    for(std::size_t i=0;i<terminal.size();++i)
        need(terminal[i].key==test.terminal[i].key&&terminal[i].value==test.terminal[i].value,"concrete terminal map");
    std::size_t blocks=0;for(const auto& fences:index.fence_keys())blocks+=fences.size();
    std::cout<<"{\"arm\":\""<<name<<"\",\"pages\":"<<index.page_count()
             <<",\"blocks\":"<<blocks<<",\"segments\":"<<index.bm_segment_count()
             <<",\"self_reported_bytes\":"<<index.memory_bytes()
             <<",\"root_self_reported_bytes\":"<<index.root_memory_bytes()<<",\"groups\":[";
    bool first=true;
    for(const auto& group:test.query.groups) {
        if(!first)std::cout<<',';first=false;
        bm_n1_leaf::Work work;std::vector<std::size_t> visited;visited.reserve(group.queries.size());
        for(const auto& query:group.queries) {
            const auto before=work.blocks;
            need(index.find_base(query.key,&work)==(query.hit?std::optional<sib::Value>(query.value):std::nullopt),"counted answer");
            visited.push_back(work.blocks-before);
        }
        std::cout<<"{\"group\":\""<<group.name<<"\",\"queries\":"<<work.queries
                 <<",\"prefix_rejects\":"<<work.rejected<<",\"model_calls\":"<<work.bm_calls
                 <<",\"proposed_blocks\":"<<work.bm_proposal_blocks<<",\"boundary_checks\":"<<work.bm_boundary_compares
                 <<",\"model_outside\":"<<work.bm_outside<<",\"fallbacks\":"<<work.fallback
                 <<",\"directory_steps\":"<<work.directory_steps<<",\"payload_blocks\":"<<work.blocks
                 <<",\"exact_compares\":"<<work.exact<<",\"visited_p50\":"<<(visited.empty()?0:percentile(visited,50))
                 <<",\"visited_p95\":"<<(visited.empty()?0:percentile(visited,95))
                 <<",\"visited_max\":"<<(visited.empty()?0:*std::max_element(visited.begin(),visited.end()))<<'}';
    }
    std::cout<<"]}\n";
}
int main(int argc,char** argv) {
    try {
        need(argc==7&&std::string(argv[1])=="profile","profile rows ops pool final queries");
        Case test(argv,0);
        bm_split_page::Index baseline(test.initial);replay_mutations(baseline,test.operations);
        profile("bm1_dynamic",baseline,test);
        bm_tx_page::Index candidate(test.initial);replay_mutations(candidate,test.operations);
        profile("tx_dynamic",candidate,test);
        bm_split_page::Index snapshot(test.terminal);profile("common_snapshot",snapshot,test);
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

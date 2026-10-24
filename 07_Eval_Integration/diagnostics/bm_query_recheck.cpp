// Public-factory query recheck. All semantic/rank validation is OUTSIDE batch timers.
#include "bm_experiment_adapter.hpp"
#include "bm_split_adapter.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace sib=string_index_bench::v1;
using Ref=std::map<std::string,std::uint64_t>;
using Clock=std::chrono::steady_clock;
static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Input {
    std::ifstream in;std::uint64_t remaining=0;
    Input(const char* path,const char* magic):in(path,std::ios::binary) {
        need(bool(in),"input unavailable");in.seekg(0,std::ios::end);auto n=in.tellg();
        need(n>=8,"truncated header");remaining=std::uint64_t(n)-8;in.seekg(0);
        char bytes[8];in.read(bytes,8);need(bool(in)&&std::memcmp(bytes,magic,8)==0,"bad input magic");
    }
    std::uint64_t number(std::size_t n=8) {
        need(n<=8&&n<=remaining,"truncated number");remaining-=n;
        unsigned char bytes[8]{};in.read(reinterpret_cast<char*>(bytes),n);need(bool(in),"short number");
        std::uint64_t value=0;for(std::size_t i=0;i<n;++i)value|=std::uint64_t(bytes[i])<<(8*i);return value;
    }
    std::string text() {
        const auto n=number();need(n<=remaining&&n<=UINT32_MAX,"invalid string length");remaining-=n;
        std::string result(n,'\0');in.read(result.data(),n);need(bool(in),"short string");return result;
    }
    void end(){need(remaining==0&&in.peek()==std::char_traits<char>::eof(),"trailing data");}
};
static std::vector<sib::Row> rows(const char* path) {
    Input input(path,"SIBROW1\0");const auto n=input.number();need(n<=10000000,"row count too large");
    std::vector<sib::Row> result;result.reserve(n);
    for(std::uint64_t i=0;i<n;++i){auto key=input.text();const auto value=input.number();result.push_back({std::move(key),value});}
    input.end();for(std::size_t i=1;i<result.size();++i)need(result[i-1].key<result[i].key,"rows not sorted unique");return result;
}
struct Op {std::uint8_t kind;std::uint64_t value,limit;std::string key;};
static std::vector<Op> ops(const char* path) {
    Input input(path,"SIBOPS1\0");const auto n=input.number();need(n<=10000000,"operation count too large");
    std::vector<Op> result;result.reserve(n);
    for(std::uint64_t i=0;i<n;++i){const auto kind=input.number(1),value=input.number(),limit=input.number();
        auto key=input.text();need(kind>=1&&kind<=5,"unknown operation");result.push_back({std::uint8_t(kind),value,limit,std::move(key)});}
    input.end();return result;
}
struct Query {std::string key;bool hit;std::uint64_t value,rank;};
struct Group {std::string name;std::vector<Query> queries;};
struct Queries {std::uint64_t lo,hi;std::vector<Group> groups;};
static Queries queries(const char* path) {
    Input input(path,"SIBQRY2\0");Queries result;result.lo=input.number();result.hi=input.number();
    const auto count=input.number();need(count>0&&count<=16,"invalid group count");
    for(std::uint64_t i=0;i<count;++i){Group group;group.name=input.text();const auto n=input.number();
        need(n<=10000,"query set too large");group.queries.reserve(n);
        for(std::uint64_t j=0;j<n;++j){auto key=input.text();const auto hit=input.number(1),value=input.number(),rank=input.number();
            need(hit<=1,"invalid presence flag");group.queries.push_back({std::move(key),bool(hit),value,rank});}
        result.groups.push_back(std::move(group));}
    input.end();return result;
}
static std::size_t initial_page(const std::vector<std::string>& lower,std::string_view key) {
    auto it=std::upper_bound(lower.begin(),lower.end(),key,[](std::string_view a,const std::string& b){return a<std::string_view(b);});
    return it==lower.begin()?0:std::size_t(it-lower.begin()-1);
}
static std::uint64_t scan_check(const std::vector<sib::Row>& got,const Ref& ref,std::string_view key,std::size_t limit) {
    auto it=ref.lower_bound(std::string(key));std::size_t n=0;
    for(const auto& row:got){need(n<limit&&it!=ref.end(),"excess scan rows");need(row.key==it->first&&row.value==it->second,"scan mismatch");++n;++it;}
    need(n==limit||it==ref.end(),"missing scan rows");return n+1;
}
static const char* names[4]={"bm_dynamic","candidate_dynamic","bm_snapshot","candidate_snapshot"};
// Williams design: every ordered adjacent pair appears once over its four rows.
static constexpr int order[4][4]={{0,1,3,2},{1,2,0,3},{2,3,1,0},{3,0,2,1}};
struct Case {
    std::vector<sib::Row> initial,pool,terminal;
    std::vector<Op> operations;Queries query;
    std::array<std::unique_ptr<sib::Index>,4> states;
    std::uint64_t checks=0,block_splits=0;
    std::size_t initial_pages=0,touched_pages=0;
    Case(char** argv,int process):initial(rows(argv[2])),pool(rows(argv[4])),terminal(rows(argv[5])),
        operations(ops(argv[3])),query(queries(argv[6])) {
        need(!initial.empty()&&!pool.empty(),"empty source/initial map");
        need(query.lo<=query.hi&&query.hi<pool.size(),"hot_range outside source pool");
        std::vector<std::string> lower{std::string{}};
        for(std::size_t i=2048;i<initial.size();i+=2048)lower.push_back(initial[i].key);
        initial_pages=lower.size();std::vector<bool> touched(initial_pages,false);
        const auto source_rank=[&](std::string_view key)->std::uint64_t {
            const auto it=std::lower_bound(pool.begin(),pool.end(),key,[](const sib::Row& row,std::string_view k){return std::string_view(row.key)<k;});
            return it!=pool.end()&&it->key==key?std::uint64_t(it-pool.begin()):UINT64_MAX;
        };
        for(int p=0;p<4;++p){const int state=order[process%4][p];
            states[state]=(state%2)?make_bm_split_experiment_index({}):make_bm_experiment_index({});
            states[state]->bulk_load(state<2?initial:terminal);
            const auto d=states[state]->descriptor();
            need(d.implementation_version==((state%2)?BM_SPLIT_CORE_SHA256:BM_CORE_BUNDLE_SHA256),"factory identity mismatch");
            need(d.adapter_version==((state%2)?BM_SPLIT_ADAPTER_SHA256:BM_EXPERIMENT_ADAPTER_SHA256),"adapter identity mismatch");}
        Ref ref;for(const auto& row:initial){need(source_rank(row.key)!=UINT64_MAX,"initial key absent from source pool");ref.emplace(row.key,row.value);}
        const auto before=bm_n1_leaf::reasons().block_splits;
        for(const auto& op:operations) {
            auto found=ref.find(op.key);const bool exists=found!=ref.end();bool writes=false;
            for(int state=0;state<2;++state) {
                auto& index=*states[state];
                switch(op.kind) {
                case 1:need(index.find(op.key)==(exists?std::optional<sib::Value>(found->second):std::nullopt),"replay find mismatch");break;
                case 2:need(index.insert(op.key,op.value)==(exists?sib::InsertResult::already_exists:sib::InsertResult::inserted),"replay insert mismatch");break;
                case 3:need(index.update(op.key,op.value)==(exists?sib::UpdateResult::updated:sib::UpdateResult::not_found),"replay update mismatch");break;
                case 4:need(index.erase(op.key)==(exists?sib::EraseResult::erased:sib::EraseResult::not_found),"replay erase mismatch");break;
                case 5:checks+=scan_check(index.scan(op.key,op.limit),ref,op.key,op.limit);break;
                }++checks;
            }
            if(op.kind==2&&!exists){need(source_rank(op.key)!=UINT64_MAX,"inserted key absent from source pool");ref.emplace(op.key,op.value);writes=true;}
            if(op.kind==3&&exists){found->second=op.value;writes=true;}
            if(op.kind==4&&exists){ref.erase(found);writes=true;}
            if(writes)touched[initial_page(lower,op.key)]=true;
        }
        block_splits=bm_n1_leaf::reasons().block_splits-before;
        touched_pages=std::count(touched.begin(),touched.end(),true);
        need(ref.size()==terminal.size(),"Python/C++ final size mismatch");
        auto it=ref.begin();for(const auto& row:terminal){need(it!=ref.end()&&row.key==it->first&&row.value==it->second,"Python/C++ final map mismatch");++it;++checks;}
        for(auto& state:states) {
            checks+=scan_check(state->scan("",SIZE_MAX),ref,"",SIZE_MAX);
            for(const auto& row:terminal){need(state->find(row.key)==std::optional<sib::Value>(row.value),"terminal find mismatch");++checks;}
        }
        for(const auto& group:query.groups) {
            std::map<std::string,bool> unique;std::size_t hits=0;
            for(const auto& q:group.queries) {
                need(unique.emplace(q.key,true).second,"duplicate query within group");
                const auto actual_rank=source_rank(q.key);need(q.rank==actual_rank,"query source rank mismatch");
                const auto expected=ref.find(q.key);need(q.hit==(expected!=ref.end()),"query presence mismatch (deleted hit or live miss)");
                need(q.hit?q.value==expected->second:q.value==0,"query expected value mismatch");
                const bool hot=actual_rank!=UINT64_MAX&&query.lo<=actual_rank&&actual_rank<=query.hi;
                const bool affected=touched[initial_page(lower,q.key)];
                if(group.name=="live_hot")need(q.hit&&hot,"live_hot grouping mismatch");
                else if(group.name=="live_nonhot")need(q.hit&&!hot&&actual_rank!=UINT64_MAX,"live_nonhot grouping mismatch");
                else if(group.name=="absent_hot")need(!q.hit&&hot,"absent_hot grouping mismatch");
                else if(group.name=="absent_nonhot")need(!q.hit&&!hot&&actual_rank!=UINT64_MAX,"absent_nonhot grouping mismatch");
                else if(group.name=="synthetic_absent")need(!q.hit&&actual_rank==UINT64_MAX,"synthetic grouping mismatch");
                else if(group.name=="live_write_touched")need(q.hit&&affected,"write-touched grouping mismatch");
                else if(group.name=="live_write_untouched")need(q.hit&&!affected,"write-untouched grouping mismatch");
                else need(group.name=="mixed90","unknown query group");
                hits+=q.hit;
                for(auto& state:states){need(state->find(q.key)==(q.hit?std::optional<sib::Value>(q.value):std::nullopt),"query answer mismatch");++checks;}
            }
            if(group.name=="mixed90")need(!group.queries.empty()&&hits*10==group.queries.size()*9,"mixed90 ratio mismatch");
        }
    }
};
static std::uint64_t word(bool hit,std::uint64_t value) {
    return hit?((value^0x517cc1b727220a95ULL)+0x9e3779b97f4a7c15ULL):0xd6e8feb86659fd93ULL;
}
static std::uint64_t expected(const Group& group,std::uint64_t loops) {
    std::uint64_t sum=0;for(const auto& q:group.queries)sum+=word(q.hit,q.value);return sum*loops;
}
static std::uint64_t lookup(const sib::Index& index,const Group& group,std::size_t loops) {
    std::uint64_t checksum=0;
    for(std::size_t pass=0;pass<loops;++pass) {
        // Once per whole set, preventing cross-pass hoisting; no CPU fence or per-key timer.
        asm volatile("" ::: "memory");
        for(const auto& q:group.queries){const auto value=index.find(q.key);checksum+=word(bool(value),value.value_or(0));}
    }
    return checksum;
}
struct Measurement {std::size_t group;int state,repeat,position;std::uint64_t ns,lookups,checksum;};
static Measurement timed(const sib::Index& index,const Group& group,std::size_t loops) {
    const auto start=Clock::now();const auto sum=lookup(index,group,loops);
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
    need(elapsed>0&&sum==expected(group,loops),"timed checksum mismatch");
    return {0,0,0,0,std::uint64_t(elapsed),std::uint64_t(loops)*group.queries.size(),sum};
}
static void metadata(const Case& test) {
    std::cout<<"\"checks\":"<<test.checks<<",\"initial_rows\":"<<test.initial.size()<<",\"final_rows\":"<<test.terminal.size()
        <<",\"source_pool_rows\":"<<test.pool.size()<<",\"initial_pages\":"<<test.initial_pages
        <<",\"write_touched_initial_pages\":"<<test.touched_pages<<",\"candidate_block_splits\":"<<test.block_splits;
}
int main(int argc,char** argv) {
    try {
        need(argc>=7,"mode rows ops pool final queries [min_ms|process loops_csv]");
        const std::string mode=argv[1];need(mode=="check"||mode=="calibrate"||mode=="bench","unknown mode");
        need((mode=="check"&&argc==7)||(mode=="calibrate"&&argc==8)||(mode=="bench"&&argc==9),"wrong argument count");
        const int process=mode=="bench"?std::stoi(argv[7]):0;need(process>=0&&process<16,"process must be 0..15");
        Case test(argv,process);
        if(mode=="check"){std::cout<<"{\"mode\":\"check\",";metadata(test);std::cout<<",\"passed\":true}\n";return 0;}
        std::vector<std::size_t> loops;
        if(mode=="calibrate") {
            const double min_ms=std::stod(argv[7]);need(min_ms>0&&min_ms<=1000,"invalid calibration duration");
            std::cout<<"{\"mode\":\"calibrate\",\"loops\":[";
            bool first=true;
            for(const auto& group:test.query.groups){std::size_t n=0;
                if(!group.queries.empty()) {
                    need(lookup(*test.states[0],group,4)==expected(group,4),"calibration warmup checksum");
                    n=1;while(true){const auto r=timed(*test.states[0],group,n);
                        if(double(r.ns)>=min_ms*1000000)break;
                        need(n<4096,"calibration exceeds loop budget");n*=2;}
                }
                if(!first)std::cout<<',';first=false;std::cout<<n;
            }
            std::cout<<"]}\n";return 0;
        }
        std::stringstream csv(argv[8]);std::string token;
        while(std::getline(csv,token,','))loops.push_back(std::stoull(token));
        need(loops.size()==test.query.groups.size(),"loop/group mismatch");
        std::vector<Measurement> result;result.reserve(test.query.groups.size()*8);
        for(std::size_t k=0;k<test.query.groups.size();++k) {
            const auto group_id=(k+std::size_t(process))%test.query.groups.size();const auto& group=test.query.groups[group_id];
            if(group.queries.empty()){need(loops[group_id]==0,"empty group loops");continue;}
            need(loops[group_id]>0&&loops[group_id]<=4096,"invalid group loops");
            for(int pos=0;pos<4;++pos){const auto state=order[process%4][pos];
                need(lookup(*test.states[state],group,4)==expected(group,4),"warmup checksum mismatch");}
            for(int repeat=0;repeat<2;++repeat)for(int pos=0;pos<4;++pos) {
                const auto state=order[process%4][repeat?3-pos:pos];
                auto r=timed(*test.states[state],group,loops[group_id]);
                r.group=group_id;r.state=state;r.repeat=repeat;r.position=pos;result.push_back(r);
            }
        }
        std::cout<<"{\"mode\":\"bench\",\"process\":"<<process<<',';metadata(test);
        std::cout<<",\"measurements\":[";bool first=true;
        for(const auto& r:result){if(!first)std::cout<<',';first=false;
            std::cout<<"{\"group\":\""<<test.query.groups[r.group].name<<"\",\"state\":\""<<names[r.state]
                <<"\",\"repeat\":"<<r.repeat<<",\"position\":"<<r.position<<",\"ns\":"<<r.ns
                <<",\"lookups\":"<<r.lookups<<",\"checksum\":"<<r.checksum<<'}';}
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

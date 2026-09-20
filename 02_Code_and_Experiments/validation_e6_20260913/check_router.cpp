// E6 router correctness gate: every variant must reproduce Index::locate exactly,
// checked per query on adversarial fence sets and after structure changes.
// Covers (review comments 2026-09-13): key == fence, between fences, below first,
// above last, empty/short keys, embedded NUL, high bytes, long common prefixes,
// 8-byte boundary pairs (abcdefgh vs abcdefghi / abcdefghz vs abcdefghaa),
// and metadata resync after insertion-triggered rebuilds/splits.
#include "route_aux.hpp"
#include <iostream>
#include <map>
#include <random>
#include <set>
using namespace validation;
static void require(bool x,const char*why){if(!x)throw std::runtime_error(why);}
using Reference=std::map<std::string,std::uint64_t,Less>;

static std::size_t verify_all(const Index& idx,RouterA&ra,RouterB&rb,const std::vector<std::string>&qs){
    std::size_t checks=0;
    for(const auto&q:qs){
        const std::size_t base=idx.locate(q);
        require(ra.locate(idx,q)==base,"RouterA locate mismatch");
        require(rb.locate(idx,q)==base,"RouterB locate mismatch");
        ++checks;
    }
    return checks;
}

int main(){try{
    std::mt19937_64 rng(20260913);
    std::set<std::string,Less> pool_set;
    // adversarial core (includes the reviewer's 8-byte boundary pairs)
    for(const auto&s:std::vector<std::string>{
        "","a","ab","abc","abcd","abcde","abcdefgh","abcdefghi","abcdefghz","abcdefghaa",
        std::string("abcdefgh\0tail",13),"abcdefgh\xff","0123456789abcdefghij",
        std::string("a\0b",3),std::string("\0",1),std::string("\xff",1),std::string("\xff\xff\xff",3),
        std::string(8,'\0'),std::string(8,'\xff'),std::string(7,'x'),std::string(9,'x'),
        std::string(4096,'q'),std::string(4096,'q')+"tail"})pool_set.insert(s);
    // long shared prefix family (fences share first 8 bytes heavily)
    for(int i=0;i<400;++i){std::string s="common/prefix/path/"+std::to_string(i);pool_set.insert(s);}
    // random binary keys
    for(int i=0;i<600;++i){std::string s=i%2?"shared/":"";
        for(std::size_t j=0,m=rng()%40;j<m;++j)s.push_back(static_cast<char>(rng()%256));
        pool_set.insert(s);}
    const std::vector<std::string> pool(pool_set.begin(),pool_set.end());

    struct Trial {std::size_t region;std::size_t step;};
    const std::vector<Trial> trials{{1,3000},{7,3000},{64,3000}};
    std::size_t checks=0,dyn_rebuilds=0;double dyn_rebuild_ns=0;
    for(const auto&tr:trials){
        Reference ref;std::vector<Pair> initial;
        for(std::size_t i=0;i<pool.size();i+=2){initial.emplace_back(pool[i],i);ref[pool[i]]=i;}
        Config c;c.region_keys=tr.region;
        Index index(c);index.bulk_load(initial);
        RouterA ra;RouterB rb;
        // battery: pool + fences + between/edge variants
        std::vector<std::string> bat=pool;
        for(std::size_t k=0;k<index.region_count();++k){
            const auto&f=index.fence(k);bat.push_back(f);
            if(!f.empty())bat.push_back(f.substr(0,f.size()-1));
            bat.push_back(f+"\x01");bat.push_back(f+"~end");
        }
        bat.push_back("");bat.push_back(std::string("\xff\xff",2));
        std::sort(bat.begin(),bat.end());bat.erase(std::unique(bat.begin(),bat.end()),bat.end());
        checks+=verify_all(index,ra,rb,bat);
        const auto da=ra.diag();
        std::cerr<<"trial region="<<tr.region<<" fences="<<da.fences<<" distinct_u="<<da.distinct_u
                 <<" max_equal_run="<<da.max_equal_run<<" equal_share="<<da.equal_share<<"\n";

        // dynamic: force rebuilds/splits, routers must resync and stay exact
        std::vector<std::string> churn;for(std::size_t i=0;i<1200;++i)churn.push_back("churn/"+std::to_string(rng()%100000));
        std::size_t lastv=index.structure_version();
        for(std::size_t step=0;step<tr.step;++step){
            const std::string&key=churn[rng()%churn.size()];const std::uint64_t value=rng();
            const auto op=rng()%3;
            if(op==0)require(index.insert(key,value)==ref.emplace(key,value).second,"insert");
            else if(op==1)require(index.erase(key)==(ref.erase(key)!=0),"erase");
            else{auto it=ref.find(key);const bool want=it==ref.end();ref[key]=value;require(index.upsert(key,value)==want,"upsert");}
            if(index.structure_version()!=lastv){
                lastv=index.structure_version();
                const std::size_t rbA0=ra.rebuilds,rbB0=rb.rebuilds;
                const double nsA0=ra.rebuild_ns,nsB0=rb.rebuild_ns;
                ra.sync(index);rb.sync(index);
                dyn_rebuilds+=ra.rebuilds-rbA0+rb.rebuilds-rbB0;
                dyn_rebuild_ns+=ra.rebuild_ns-nsA0+rb.rebuild_ns-nsB0;
            }
            if(step%37==0){
                for(int q=0;q<24;++q){const auto&k2=pool[rng()%pool.size()];
                    const auto base=index.locate(k2);
                    require(ra.locate(index,k2)==base,"RouterA dynamic mismatch");
                    require(rb.locate(index,k2)==base,"RouterB dynamic mismatch");++checks;}
            }
            if(step%500==0)checks+=verify_all(index,ra,rb,bat);
        }
        checks+=verify_all(index,ra,rb,bat);
        std::size_t fb=0;for(std::size_t k=0;k<index.region_count();++k)fb+=index.fence(k).capacity()+1;
        std::cerr<<"trial done: regions="<<index.region_count()<<" RouterA_aux_bytes="<<ra.metadata_bytes()
                 <<" RouterB_aux_bytes="<<rb.metadata_bytes()<<" fences_bytes~"<<fb<<"\n";
    }
    std::cout<<"{\"status\":\"passed\",\"locate_checks\":"<<checks
        <<",\"dynamic_meta_rebuilds\":"<<dyn_rebuilds
        <<",\"dynamic_meta_rebuild_ns\":"<<dyn_rebuild_ns
        <<",\"seed\":20260913}"<<std::endl;
}catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<std::endl;return 1;}}

// Current bounded PUBLIC factories: deterministic acceptance, never a timing result.
#include "bm_followup_memory.hpp"
#include "bm_experiment_adapter.hpp"
#include "bm_split_adapter.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace sib=string_index_bench::v1;
namespace mem=bm_followup_memory;
using Ref=std::map<std::string,std::uint64_t>;
struct Op {std::uint8_t kind;std::uint64_t value,limit;std::string key;};
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct Reader {
    std::ifstream stream;
    std::uint64_t remaining=0;
    explicit Reader(const char* path,const char* magic):stream(path,std::ios::binary) {
        require(bool(stream),"binary input unavailable");
        stream.seekg(0,std::ios::end);const auto size=stream.tellg();
        require(size>=8,"truncated binary header");remaining=static_cast<std::uint64_t>(size)-8;stream.seekg(0);
        char bytes[8];stream.read(bytes,8);
        require(bool(stream)&&std::memcmp(bytes,magic,8)==0,"invalid binary header");
    }
    std::uint64_t number(std::size_t n=8) {
        require(n<=remaining,"truncated binary number");remaining-=n;
        unsigned char bytes[8]{};stream.read(reinterpret_cast<char*>(bytes),n);
        require(bool(stream),"truncated binary number");
        std::uint64_t result=0;for(std::size_t i=0;i<n;++i)result|=std::uint64_t(bytes[i])<<(8*i);
        return result;
    }
    std::string key(std::uint64_t n) {
        require(n<=UINT32_MAX,"oversized binary key");
        require(n<=remaining,"truncated binary key");remaining-=n;
        std::string result(n,'\0');stream.read(result.data(),n);
        require(bool(stream),"truncated binary key");return result;
    }
    void finish(){require(remaining==0&&stream.peek()==std::char_traits<char>::eof(),"trailing binary data");}
};
static void allocator_self_check() {
    void* input=::operator new(37); // Allocated before the measured epoch.
    mem::begin();
    void* scalar=::operator new(17);
    void* array=::operator new[](31);
    void* aligned=::operator new(128,std::align_val_t(64));
    void* no_throw=::operator new(19,std::nothrow);
    require(mem::live==195&&mem::peak==195&&mem::requests==4,"requested-byte self check");
    require(reinterpret_cast<std::uintptr_t>(aligned)%64==0,"aligned allocation self check");
    mem::tracking=false;::operator delete(input);
    require(mem::live==195,"pre-epoch free changed ledger");
    ::operator delete(scalar);::operator delete[](array);
    ::operator delete(aligned,std::align_val_t(64));::operator delete(no_throw,std::nothrow);
    require(mem::live==0,"paused free did not clear epoch ledger");
    mem::arm(1);void* failed=::operator new(19,std::nothrow);const auto calls=mem::disarm();
    require(failed==nullptr&&calls==1&&mem::live==0,"nothrow fault self check");
}
static std::vector<sib::Row> read_rows(const char* path) {
    Reader input(path,"SIBROW1\0");const auto count=input.number();
    require(count<=10000000,"oversized row count");std::vector<sib::Row> rows;rows.reserve(count);
    for(std::uint64_t i=0;i<count;++i){auto key=input.key(input.number());const auto value=input.number();rows.push_back({std::move(key),value});}
    input.finish();for(std::size_t i=1;i<rows.size();++i)require(rows[i-1].key<rows[i].key,"rows not sorted unique");
    return rows;
}
static std::vector<Op> read_ops(const char* path) {
    Reader input(path,"SIBOPS1\0");const auto count=input.number();
    require(count<=10000000,"oversized operation count");std::vector<Op> operations;operations.reserve(count);
    for(std::uint64_t i=0;i<count;++i){const auto kind=input.number(1),value=input.number(),limit=input.number();
        auto key=input.key(input.number());require(kind>=1&&kind<=5,"unknown operation");
        operations.push_back({static_cast<std::uint8_t>(kind),value,limit,std::move(key)});}
    input.finish();return operations;
}
static std::unique_ptr<sib::Index> factory(bool candidate) {
    return candidate?make_bm_split_experiment_index({}):make_bm_experiment_index({});
}
static std::uint64_t scan_check(sib::Index& index,const Ref& reference,std::string_view lower,std::size_t limit) {
    const auto got=index.scan(lower,limit);auto it=reference.lower_bound(std::string(lower));
    std::size_t size=0;
    for(const auto& row:got){require(it!=reference.end()&&size<limit,"scan excess rows");
        require(row.key==it->first&&row.value==it->second,"scan key/value/order mismatch");++it;++size;}
    require(size==limit||it==reference.end(),"scan missing rows");return size+1;
}
static std::uint64_t full_check(sib::Index& index,const Ref& reference) {
    auto checks=scan_check(index,reference,"",SIZE_MAX);
    for(const auto& [key,value]:reference){require(index.find(key)==std::optional<sib::Value>(value),"find lost/changed key");++checks;}
    return checks;
}
static std::uint64_t checked_op(sib::Index& index,Ref& reference,const Op& op) {
    const auto it=reference.find(op.key);const bool exists=it!=reference.end();
    switch(op.kind) {
    case 1:require(index.find(op.key)==(exists?std::optional<sib::Value>(it->second):std::nullopt),"find mismatch");break;
    case 2:require(index.insert(op.key,op.value)==(exists?sib::InsertResult::already_exists:sib::InsertResult::inserted),"insert mismatch");reference.emplace(op.key,op.value);break;
    case 3:require(index.update(op.key,op.value)==(exists?sib::UpdateResult::updated:sib::UpdateResult::not_found),"update mismatch");if(exists)it->second=op.value;break;
    case 4:require(index.erase(op.key)==(exists?sib::EraseResult::erased:sib::EraseResult::not_found),"erase mismatch");if(exists)reference.erase(it);break;
    case 5:return scan_check(index,reference,op.key,op.limit);
    default:throw std::runtime_error("bad operation");
    }return 1;
}
static std::uint64_t raw_op(sib::Index& index,const Op& op) {
    switch(op.kind) {
    case 1:return index.find(op.key).value_or(UINT64_MAX);
    case 2:return static_cast<std::uint64_t>(index.insert(op.key,op.value));
    case 3:return static_cast<std::uint64_t>(index.update(op.key,op.value));
    case 4:return static_cast<std::uint64_t>(index.erase(op.key));
    case 5:{const auto rows=index.scan(op.key,op.limit);std::uint64_t sum=rows.size();for(const auto& row:rows)sum=sum*131+row.value;return sum;}
    default:throw std::runtime_error("bad operation");
    }
}
static void replay(const char* rows_path,const char* ops_path,bool space) {
    const auto rows=read_rows(rows_path);const auto operations=read_ops(ops_path);
    for(const bool candidate:{false,true}) {
        if(!space) {
            Ref reference;for(const auto& row:rows)reference.emplace(row.key,row.value);
            auto index=factory(candidate);index->bulk_load(rows);std::uint64_t checks=full_check(*index,reference);
            for(const auto& op:operations)checks+=checked_op(*index,reference,op);
            checks+=full_check(*index,reference);
            std::cout<<"{\"mode\":\"differential\",\"arm\":\""<<(candidate?"candidate":"baseline")
                <<"\",\"operations\":"<<operations.size()<<",\"final_rows\":"<<reference.size()
                <<",\"checks\":"<<checks<<",\"mismatches\":0}"<<std::endl;
        }else {
            // No oracle mutation, input copy, I/O, descriptor string or self-report string in epoch.
            mem::begin();auto index=factory(candidate);index->bulk_load(rows);
            const auto build_peak=mem::peak,initial=mem::live;mem::peak=mem::live;
            std::uint64_t checksum=0,find_allocations=0;
            for(const auto& op:operations){const auto before=mem::requests;checksum=checksum*131+raw_op(*index,op);
                if(op.kind==1)find_allocations+=mem::requests-before;}
            const auto terminal=mem::live,maintenance_peak=mem::peak;
            index.reset();mem::tracking=false;require(mem::live==0,"space ledger did not return to zero");
            require(find_allocations==0,"find allocated");
            std::cout<<"{\"mode\":\"space\",\"arm\":\""<<(candidate?"candidate":"baseline")
                <<"\",\"initial_requested\":"<<initial<<",\"terminal_requested\":"<<terminal
                <<",\"build_peak_requested\":"<<build_peak<<",\"maintenance_peak_requested\":"<<maintenance_peak
                <<",\"overall_peak_requested\":"<<std::max(build_peak,maintenance_peak)
                <<",\"find_allocations\":"<<find_allocations<<",\"after_destroy\":"<<mem::live
                <<",\"checksum\":"<<checksum<<"}"<<std::endl;
        }
    }
}

struct Case {std::string name;std::vector<sib::Row> rows;std::string prime;Op operation;};
static std::vector<sib::Row> byte_rows() {
    Ref keys{{"",1},{std::string(1,'\0'),2},{std::string("\0\xff",2),3},{std::string(1,'\xff'),4}};
    for(std::uint64_t i=0;i<2044;++i){std::string key="byte/";for(int shift:{24,16,8,0})key.push_back(static_cast<char>((i>>shift)&255));keys.emplace(key,100+i);}
    std::vector<sib::Row> rows;for(const auto& [key,value]:keys)rows.push_back({key,value});return rows;
}
static std::vector<Case> cases(std::vector<sib::Row> source) {
    require(source.size()>=2048,"fault fixture requires 2048 rows");source.resize(2048);
    std::vector<sib::Row> small(source.begin(),source.begin()+1024);
    const std::string root=small[1].key;
    std::size_t bytes=0;for(const auto& row:small)bytes+=row.key.size();
    auto prefix=small;for(auto& row:prefix)row.key="fixture-prefix/"+row.key;
    return {
        {"dirty_insert",small,"",{2,900001,0,small[500].key+std::string("\0new",4)}},
        {"block_split",small,root+std::string("\0a",2),{2,900002,0,root+std::string("\0b",2)}},
        {"page_full",source,"",{2,900003,0,source[500].key+std::string("\0full",5)}},
        {"prefix_change",prefix,"",{2,900004,0,"outside-prefix"}},
        {"arena_short",small,"",{2,900005,0,small[500].key+std::string(bytes+4096,'x')}},
        {"dirty_erase",small,"",{4,0,0,small[100].key}},
        {"bulk_load",small,"",{0,0,0,""}},
    };
}
static void establish(sib::Index& index,const Case& test,Ref& reference) {
    for(const auto& row:test.rows)reference.emplace(row.key,row.value);
    if(!test.prime.empty()){require(index.insert(test.prime,900000)==sib::InsertResult::inserted,"prime insert failed");reference.emplace(test.prime,900000);}
}
static std::uint64_t continue_ops(sib::Index& index,Ref& reference,const Case& test,std::size_t count) {
    std::uint64_t checks=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& original=test.rows[(i*13)%test.rows.size()].key;
        Op op{1,800000+i,0,original};
        switch(i%5) {
        case 0:op.kind=2;op.key=original+"~continue/"+std::to_string(i);break;
        case 1:op.kind=3;break;
        case 2:op.kind=4;break;
        case 3:break;
        case 4:op.kind=5;op.limit=(i%2)?16U:128U;break;
        }
        // Oracle and returned scan storage are not part of the fault operation ledger.
        checks+=checked_op(index,reference,op);
    }
    return checks+full_check(index,reference);
}
// Literal plan-2.3 continuation: pure insert/delete churn, nothing else.
static std::uint64_t continue_writes(sib::Index& index,Ref& reference,const Case& test,std::size_t count) {
    std::uint64_t checks=0;
    for(std::size_t i=0;i<count;i+=2) {
        const std::string key=test.rows[(i*13)%test.rows.size()].key+"~writes/"+std::to_string(i);
        checks+=checked_op(index,reference,Op{2,700000+i,0,key});
        if(i+1<count)checks+=checked_op(index,reference,Op{4,0,0,key});
    }
    return checks+full_check(index,reference);
}
static void fault(const char* fixture,std::size_t per_fault_mixed,std::size_t per_fault_writes) {
    const auto source=std::string(fixture)=="--bytes"?byte_rows():read_rows(fixture);
    for(const auto& test:cases(source)) {
        std::uint64_t allocations=0,checks=0,throws=0;
        // Preflight proves path coverage and obtains the COMPLETE allocation ordinal range.
        {
            auto index=factory(true);if(test.name!="bulk_load")index->bulk_load(test.rows);
            Ref reference;if(test.name!="bulk_load")establish(*index,test,reference);
            const auto before=bm_n1_leaf::reasons();
            mem::arm(0);if(test.name=="bulk_load")index->bulk_load(test.rows);else raw_op(*index,test.operation);
            allocations=mem::disarm();const auto after=bm_n1_leaf::reasons();
            require(allocations>0,"fault case has no allocations");
            if(test.name=="block_split")require(after.block_splits==before.block_splits+1,"block split not triggered");
            if(test.name=="page_full")require(after.page_full==before.page_full+1,"page full not triggered");
            if(test.name=="prefix_change")require(after.prefix_change==before.prefix_change+1,"prefix change not triggered");
            if(test.name=="arena_short")require(after.arena_short==before.arena_short+1,"arena exhaustion not triggered");
        }
        std::cout<<"{\"mode\":\"fault_preflight\",\"case\":\""<<test.name<<"\",\"allocation_ordinals\":"<<allocations<<"}"<<std::endl;
        for(std::uint64_t ordinal=1;ordinal<=allocations+1;++ordinal) {
            Ref reference; // Owns no tracked allocations.
            mem::begin();auto index=factory(true);
            if(test.name!="bulk_load")index->bulk_load(test.rows);
            mem::tracking=false;
            if(test.name!="bulk_load") {
                for(const auto& row:test.rows)reference.emplace(row.key,row.value);
                if(!test.prime.empty()) {
                    mem::tracking=true;require(index->insert(test.prime,900000)==sib::InsertResult::inserted,"prime failed");mem::tracking=false;
                    reference.emplace(test.prime,900000);
                }
            }
            const auto before=mem::live;bool threw=false;
            mem::tracking=true;mem::arm(ordinal);
            try{if(test.name=="bulk_load")index->bulk_load(test.rows);else raw_op(*index,test.operation);}
            catch(const std::bad_alloc&){threw=true;++throws;}
            mem::disarm();mem::tracking=false;
            require(threw==(ordinal<=allocations),"allocation ordinal coverage changed");
            if(threw) {
                require(mem::live==before,"failed operation retained/released requested bytes");
                if(test.name!="bulk_load") {
                    checks+=full_check(*index,reference);
                    if(test.operation.kind==2)require(!index->find(test.operation.key),"failed insertion was published");
                }
                mem::tracking=true;
                if(test.name=="bulk_load")index->bulk_load(test.rows);else raw_op(*index,test.operation);
                mem::tracking=false;
            }
            if(test.name=="bulk_load"){for(const auto& row:test.rows)reference.emplace(row.key,row.value);}
            else if(test.operation.kind==2)reference.emplace(test.operation.key,test.operation.value);
            else if(test.operation.kind==4)reference.erase(test.operation.key);
            checks+=full_check(*index,reference);
            // Mixed continuation after EVERY fault (S0 continuity); the positive control
            // gets a longer churn, and the literal insert/delete churn runs on request.
            checks+=continue_ops(*index,reference,test,ordinal==allocations+1?1000:per_fault_mixed);
            if(per_fault_writes)checks+=continue_writes(*index,reference,test,per_fault_writes);
            index.reset();require(mem::live==0,"fault trial leaked tracked requested bytes");
        }
        std::cout<<"{\"mode\":\"fault\",\"case\":\""<<test.name<<"\",\"allocation_ordinals\":"<<allocations
            <<",\"bad_alloc_caught\":"<<throws<<",\"positive_controls\":1"
            <<",\"continuation_per_fault_mixed\":"<<per_fault_mixed
            <<",\"continuation_per_fault_writes\":"<<per_fault_writes
            <<",\"continuation_per_path\":1000,\"checks\":"<<checks<<",\"mismatches\":0,\"after_destroy\":0}"<<std::endl;
    }
}
static void dense(const char* fixture,std::size_t count) {
    const auto rows=std::string(fixture)=="--bytes"?byte_rows():read_rows(fixture);
    require(!rows.empty()&&count>0&&count<=100000,"invalid dense fixture");
    const auto root=rows[rows.size()/2].key;
    for(const bool candidate:{false,true}) {
        Ref reference;for(const auto& row:rows)reference.emplace(row.key,row.value);
        auto index=factory(candidate);index->bulk_load(rows);
        std::uint64_t checks=full_check(*index,reference);
        const auto splits_before=bm_n1_leaf::reasons().block_splits;
        for(std::size_t i=0;i<count;++i) {
            const auto key=root+std::string("\0dense/",7)+std::to_string(i);
            require(reference.find(key)==reference.end(),"dense key already exists");
            checks+=checked_op(*index,reference,Op{2,5000000+i,0,key});
            checks+=checked_op(*index,reference,Op{1,0,0,key});
            if(i%64==0) {
                checks+=checked_op(*index,reference,Op{2,0,0,key}); // Duplicate preserves value.
                checks+=checked_op(*index,reference,Op{5,0,(i%128)?16U:128U,key});
            }
            if(i%128==0)checks+=checked_op(*index,reference,Op{3,6000000+i,0,root});
            if(i%256==0) {
                checks+=checked_op(*index,reference,Op{4,0,0,key});
                checks+=checked_op(*index,reference,Op{2,5000000+i,0,key});
            }
            if((i+1)%1000==0)checks+=full_check(*index,reference);
        }
        checks+=full_check(*index,reference);
        const auto splits=bm_n1_leaf::reasons().block_splits-splits_before;
        if(candidate)require(splits>0,"dense factory test did not trigger block split");
        std::cout<<"{\"mode\":\"dense_factory\",\"arm\":\""<<(candidate?"candidate":"baseline")
            <<"\",\"initial_rows\":"<<rows.size()<<",\"dense_inserts\":"<<count
            <<",\"final_rows\":"<<reference.size()<<",\"candidate_block_splits\":"<<splits
            <<",\"checks\":"<<checks<<",\"mismatches\":0}"<<std::endl;
    }
}
int main(int argc,char** argv) {
    try {
        allocator_self_check();
        if(argc==3&&std::string(argv[1])=="fault")fault(argv[2],5,0);
        else if(argc==4&&std::string(argv[1])=="fault") {
            // The count is the literal plan-2.3 insert/delete continuation; the mixed
            // continuation stays at 5 so the S0 profile is preserved.
            const auto writes=std::stoull(argv[3]);
            if(!writes||writes>1000000)throw std::invalid_argument("write continuations must be 1..1000000");
            fault(argv[2],5,static_cast<std::size_t>(writes));
        }
        else if(argc==4&&std::string(argv[1])=="dense")dense(argv[2],std::stoull(argv[3]));
        else if(argc==4&&(std::string(argv[1])=="differential"||std::string(argv[1])=="space"))replay(argv[2],argv[3],std::string(argv[1])=="space");
        else throw std::invalid_argument("fault rows.bin|--bytes [insert_delete_continuations] OR dense rows.bin|--bytes count OR differential|space rows.bin ops.bin");
        return 0;
    }catch(const std::exception& e){mem::disarm();mem::tracking=false;std::cerr<<e.what()<<std::endl;return 1;}
}

#pragma once
// Independent, precomputed expected answers; never part of measured index storage.
#include "index_contract.h"
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace bm_core_oracle {
namespace sib=string_index_bench::v1;
inline void require(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
struct Answer {std::optional<sib::Value> found;bool changed=false;std::vector<sib::Row> scan;};
class Trace {
    std::vector<Answer> answers_;
public:
    template<class Operations> Trace(const std::vector<sib::Row>& initial,const Operations& operations,
                                     const std::vector<sib::Row>& terminal) {
        std::map<std::string,sib::Value> oracle;
        for(const auto& row:initial)require(oracle.emplace(row.key,row.value).second,"oracle initial unique");
        answers_.reserve(operations.size());
        for(const auto& op:operations) {
            Answer answer;auto found=oracle.find(op.key);
            switch(op.kind) {
            case 1:if(found!=oracle.end())answer.found=found->second;break;
            case 2:answer.changed=oracle.emplace(op.key,op.value).second;break;
            case 3:if(found!=oracle.end()){found->second=op.value;answer.changed=true;}break;
            case 4:if(found!=oracle.end()){oracle.erase(found);answer.changed=true;}break;
            case 5:
                for(auto it=oracle.lower_bound(op.key);it!=oracle.end()&&answer.scan.size()<op.limit;++it)
                    answer.scan.push_back({it->first,it->second});
                break;
            default:throw std::runtime_error("oracle operation kind");
            }
            answers_.push_back(std::move(answer));
        }
        require(oracle.size()==terminal.size(),"oracle terminal size");auto it=oracle.begin();
        for(const auto& row:terminal){require(it->first==row.key&&it->second==row.value,"oracle terminal rows");++it;}
    }
    template<class Index,class Operations> void replay(Index& index,const Operations& operations)const {
        require(operations.size()==answers_.size(),"oracle operation count");
        for(std::size_t i=0;i<operations.size();++i) {
            const auto& op=operations[i];const auto& expected=answers_[i];
            switch(op.kind) {
            case 1:require(index.find_base(op.key)==expected.found,"oracle trace find");break;
            case 2:require(index.insert(op.key,op.value)==expected.changed,"oracle trace insert");break;
            case 3:require(index.update(op.key,op.value)==expected.changed,"oracle trace update");break;
            case 4:require(index.erase(op.key)==expected.changed,"oracle trace erase");break;
            case 5:{
                const auto actual=index.scan(op.key,op.limit);require(actual.size()==expected.scan.size(),"oracle trace scan count");
                for(std::size_t j=0;j<actual.size();++j)
                    require(actual[j].key==expected.scan[j].key&&actual[j].value==expected.scan[j].value,"oracle trace scan rows");
                break;
            }
            }
        }
    }
};
}

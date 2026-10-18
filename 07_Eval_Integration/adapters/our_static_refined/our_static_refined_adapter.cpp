#include "our_static_refined_adapter.h"
#include "collision_refined_index.hpp"

#ifndef REFINED_CORE_BUNDLE_SHA256
#error "bind the complete implementation header bundle"
#endif
#ifndef REFINED_ADAPTER_SHA256
#error "bind the actual adapter source/header digest"
#endif
namespace sib=string_index_bench::v1;
namespace {
template<bool Refined> class Adapter final:public sib::Index {
    std::unique_ptr<const collision_refined::Index> index_;
    const collision_refined::Index& built()const {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
public:
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;d.implementation="StaticPhysicalIndex local-prefix refinement";
        d.implementation_version=REFINED_CORE_BUNDLE_SHA256;d.adapter_version=REFINED_ADAPTER_SHA256;
        d.capabilities.bulk_load=d.capabilities.find=d.capabilities.scan=sib::Support::native;
        d.capabilities.keys.max_bytes=UINT32_MAX;
        d.adaptation_notes="Explicit immutable package; one successful build; owning scan; "
            "32-bit key/suffix-arena/block limits. Insert/update/erase/range_sum unsupported. "
            "One local prefix level, certified physical ranges and exact parent correction. Lookup=";
        d.adaptation_notes+=Refined?"refined":"directory";return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("second bulk_load is not allowed");
        auto next=std::make_unique<collision_refined::Index>(rows);index_=std::move(next);
    }
    std::optional<sib::Value> find(std::string_view key)const override {
        if constexpr(Refined)return built().find_model(key);
        else return built().find_directory(key);
    }
    sib::InsertResult insert(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("read-only insert");}
    sib::UpdateResult update(std::string_view,sib::Value)override{throw sib::UnsupportedOperation("read-only update");}
    sib::EraseResult erase(std::string_view)override{throw sib::UnsupportedOperation("read-only erase");}
    sib::Value range_sum(std::string_view,std::string_view)const override{throw sib::UnsupportedOperation("range_sum unsupported");}
    std::vector<sib::Row> scan(std::string_view key,std::size_t limit)const override {
        auto rows=built().scan(key,limit);std::vector<sib::Row> out;out.reserve(rows.size());
        for(auto& r:rows)out.push_back({std::move(r.key),r.value});return out;
    }
    sib::MemoryUsage memory_usage()const override {
        sib::MemoryUsage m;m.index_owned_bytes=index_?index_->memory().total():0;
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;
        m.accounting_notes="Object and vector/string capacities including refinement; adapter separate. "
            "Excludes allocator/RSS/build peak and returned scan rows.";return m;
    }
};
}
std::unique_ptr<sib::Index> make_our_static_refined_index(const sib::Parameters& parameters) {
    std::string lookup="refined";bool seen=false;
    for(const auto& p:parameters) {
        if(p.first!="lookup"||seen)throw std::invalid_argument("unknown or duplicate parameter");
        seen=true;lookup=p.second;
    }
    if(lookup=="refined")return std::make_unique<Adapter<true>>();
    if(lookup=="directory")return std::make_unique<Adapter<false>>();
    throw std::invalid_argument("lookup must be refined or directory");
}

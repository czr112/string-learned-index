#pragma once
// D1 experimental interface. B and D1 use exactly this owning wrapper.
#include "index_contract.h"
#include "collision_refined_index.hpp"
#include <type_traits>

#ifndef REFINEMENT_BASE_SHA256
#error "bind the original core identity"
#endif
#ifndef REFINEMENT_CORE_SHA256
#error "bind the seam core identity"
#endif
#ifndef REFINEMENT_ADAPTER_SHA256
#error "bind this diagnostic interface identity"
#endif

namespace refinement_adapter {
namespace sib=string_index_bench::v1;
template<class Core,static_physical::Lookup Policy> class Adapter final:public sib::Index {
    std::unique_ptr<const Core> index_;
    const Core& built()const {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
public:
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;
        if constexpr(std::is_same_v<Core,collision_refined::Index>) {
            d.implementation="D1 collision refinement (experimental)";d.implementation_version=REFINEMENT_CORE_SHA256;
        } else {d.implementation="B physical locator control";d.implementation_version=REFINEMENT_BASE_SHA256;}
        d.adapter_version=REFINEMENT_ADAPTER_SHA256;
        d.capabilities.bulk_load=d.capabilities.find=d.capabilities.scan=sib::Support::native;
        d.capabilities.keys.max_bytes=UINT32_MAX;
        d.adaptation_notes="Owning immutable diagnostic interface; one successful build; owning scan; "
            "32-bit aggregate suffix arena and block limits; updates/range_sum unsupported.";
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("second bulk_load is not allowed");
        auto next=std::make_unique<Core>(rows);index_=std::move(next);
    }
    std::optional<sib::Value> find(std::string_view key)const override {
        if constexpr(Policy==static_physical::Lookup::learned)return built().find_model(key);
        else if constexpr(Policy==static_physical::Lookup::directory)return built().find_directory(key);
        else return built().find_hybrid(key);
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
        m.accounting_notes="Core object and capacities; adapter separate; excludes allocator, RSS, peak, scan results.";
        return m;
    }
};
} // namespace refinement_adapter

inline std::unique_ptr<string_index_bench::v1::Index> make_collision_refined_index(
    const string_index_bench::v1::Parameters& parameters) {
    using static_physical::Lookup;
    std::string policy="learned";bool seen=false;
    for(const auto& p:parameters) {
        if(p.first!="lookup"||seen)throw std::invalid_argument("unknown or duplicate parameter");
        seen=true;policy=p.second;
    }
    if(policy=="learned")return std::make_unique<refinement_adapter::Adapter<collision_refined::Index,Lookup::learned>>();
    if(policy=="directory")return std::make_unique<refinement_adapter::Adapter<collision_refined::Index,Lookup::directory>>();
    if(policy=="wide_directory")return std::make_unique<refinement_adapter::Adapter<collision_refined::Index,Lookup::wide_directory>>();
    throw std::invalid_argument("lookup must be learned, directory or wide_directory");
}

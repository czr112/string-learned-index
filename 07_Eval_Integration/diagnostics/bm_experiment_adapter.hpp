#pragma once
// R0 unified experiment factory: bounded build, per-page conversion, base lookup by default.
// It does not replace make_bm_page_index; the old package keeps its own identity.
#include "index_contract.h"
#include "bm_bounded_index.hpp"

#ifndef BM_MODEL_COMMIT
#error "bind the frozen BlockModel commit"
#endif
#ifndef BM_CORE_BUNDLE_SHA256
#error "bind the bounded core bundle"
#endif
#ifndef BM_EXPERIMENT_ADAPTER_SHA256
#error "bind this adapter source"
#endif

namespace bm_experiment {
namespace sib=string_index_bench::v1;
class Adapter final:public sib::Index {
public:
    enum class Lookup {base,window,directory};
private:
    std::unique_ptr<bm_bounded_page::Index> index_;
    Lookup lookup_;
    const char* lookup_name()const {
        return lookup_==Lookup::base?"base":lookup_==Lookup::window?"window":"directory";
    }
    const bm_bounded_page::Index& built()const {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
    bm_bounded_page::Index& mutable_built() {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
public:
    explicit Adapter(Lookup lookup):lookup_(lookup) {}
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;
        d.implementation=lookup_==Lookup::base?"BM page (bounded build, base lookup)":
                         lookup_==Lookup::window?"BM page (bounded build, window lookup)":
                                                 "BM page (bounded build, directory lookup)";
        d.implementation_version=BM_CORE_BUNDLE_SHA256;
        d.adapter_version=BM_EXPERIMENT_ADAPTER_SHA256;
        auto& c=d.capabilities;
        c.bulk_load=c.find=c.insert=c.update=c.erase=c.scan=sib::Support::native;
        c.keys.max_bytes=UINT32_MAX;
        d.adaptation_notes="model_commit=" BM_MODEL_COMMIT "; build_mode=bounded-per-page; lookup_mode=";
        d.adaptation_notes+=lookup_name();d.adaptation_notes+="; root_mode=flat; single-threaded; "
            "range_sum unsupported; 2048 rows/page, 15/16 slots, full-key verification.";
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("second bulk_load is not allowed");
        auto next=std::make_unique<bm_bounded_page::Index>(rows);
        index_=std::move(next);
    }
    std::optional<sib::Value> find(std::string_view key)const override {
        switch(lookup_) {
            case Lookup::window:return built().find(key);
            case Lookup::directory:return built().find_exact(key);
            default:return built().find_base(key);
        }
    }
    sib::InsertResult insert(std::string_view key,sib::Value value)override {
        return mutable_built().insert(key,value)?sib::InsertResult::inserted:sib::InsertResult::already_exists;
    }
    sib::UpdateResult update(std::string_view key,sib::Value value)override {
        return mutable_built().update(key,value)?sib::UpdateResult::updated:sib::UpdateResult::not_found;
    }
    sib::EraseResult erase(std::string_view key)override {
        return mutable_built().erase(key)?sib::EraseResult::erased:sib::EraseResult::not_found;
    }
    sib::Value range_sum(std::string_view,std::string_view)const override {
        throw sib::UnsupportedOperation("range_sum unsupported");
    }
    std::vector<sib::Row> scan(std::string_view lower,std::size_t limit)const override {
        auto rows=built().scan(lower,limit);
        std::vector<sib::Row> out;out.reserve(rows.size());
        for(auto& row:rows)out.push_back({std::move(row.key),row.value});
        return out;
    }
    sib::MemoryUsage memory_usage()const override {
        sib::MemoryUsage m;m.index_owned_bytes=index_?index_->memory_bytes():0;
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;
        m.accounting_notes="Bounded page index self-report: pages, root, model, fences. "
            "Excludes allocator, build peak and returned scan rows.";return m;
    }
};
} // namespace bm_experiment

inline std::unique_ptr<string_index_bench::v1::Index> make_bm_experiment_index(
    const string_index_bench::v1::Parameters& parameters) {
    bm_experiment::Adapter::Lookup lookup=bm_experiment::Adapter::Lookup::base;bool seen=false;
    for(const auto& parameter:parameters) {
        if(parameter.first!="lookup"||seen)throw std::invalid_argument("unknown or duplicate parameter");
        seen=true;
        if(parameter.second=="base")lookup=bm_experiment::Adapter::Lookup::base;
        else if(parameter.second=="window")lookup=bm_experiment::Adapter::Lookup::window;
        else if(parameter.second=="directory")lookup=bm_experiment::Adapter::Lookup::directory;
        else throw std::invalid_argument("lookup must be base, window or directory");
    }
    return std::make_unique<bm_experiment::Adapter>(lookup);
}

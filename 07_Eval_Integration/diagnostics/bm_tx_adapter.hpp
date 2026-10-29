#pragma once
// Formal experiment factory for the transactional split candidate (N3), independent of the
// production make_bm_page_index and of the baseline make_bm_experiment_index.
#include "index_contract.h"
#include "bm_tx_index.hpp"

#ifndef BM_MODEL_COMMIT
#error "bind the frozen BlockModel commit"
#endif
#ifndef BM_TX_CORE_SHA256
#error "bind the transactional split core bundle"
#endif
#ifndef BM_TX_ADAPTER_SHA256
#error "bind this adapter source"
#endif

namespace bm_tx_experiment {
namespace sib=string_index_bench::v1;
class Adapter final:public sib::Index {
    std::unique_ptr<bm_tx_page::Index> index_;
    const bm_tx_page::Index& built()const {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
    bm_tx_page::Index& mut() {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
public:
    std::size_t verify()const{return built().verify();}
    const bm_tx_page::Maintenance& maintenance()const{return built().maintenance();}
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;
        d.implementation="BM page (transactional encoded split)";
        d.implementation_version=BM_TX_CORE_SHA256;
        d.adapter_version=BM_TX_ADAPTER_SHA256;
        auto& c=d.capabilities;
        c.bulk_load=c.find=c.insert=c.update=c.erase=c.scan=sib::Support::native;
        c.keys.max_bytes=UINT32_MAX;
        d.adaptation_notes="model_commit=" BM_MODEL_COMMIT "; build_mode=bounded-per-page; "
            "mutate_mode=encoded-page-split; lookup_mode=base; root_mode=flat; single-threaded; "
            "2048 rows/page, 15/16 slots, strong exception guarantee on block split.";
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("second bulk_load is not allowed");
        // Bounded build: the page constructor converts page by page from these rows, so no
        // whole-input copy is materialised here.
        index_=std::make_unique<bm_tx_page::Index>(rows);
    }
    std::optional<sib::Value> find(std::string_view key)const override {return built().find_base(key);}
    sib::InsertResult insert(std::string_view key,sib::Value value)override {
        return mut().insert(key,value)?sib::InsertResult::inserted:sib::InsertResult::already_exists;
    }
    sib::UpdateResult update(std::string_view key,sib::Value value)override {
        return mut().update(key,value)?sib::UpdateResult::updated:sib::UpdateResult::not_found;
    }
    sib::EraseResult erase(std::string_view key)override {
        return mut().erase(key)?sib::EraseResult::erased:sib::EraseResult::not_found;
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
        m.accounting_notes="transactional split candidate self-report";return m;
    }
};
} // namespace bm_tx_experiment

inline std::unique_ptr<string_index_bench::v1::Index> make_bm_tx_experiment_index(
    const string_index_bench::v1::Parameters& parameters) {
    if(!parameters.empty())throw std::invalid_argument("the transactional split factory takes no parameters");
    return std::make_unique<bm_tx_experiment::Adapter>();
}

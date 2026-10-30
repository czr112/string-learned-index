#pragma once
// Explicit read-only experiment. The runner binds the nominated generated core.
#include "index_contract.h"
#include BM_MODEL16_INDEX_HEADER

#ifndef BM_MODEL_COMMIT
#error "bind the frozen upstream model commit"
#endif
#ifndef BM_TX_CORE_SHA256
#error "bind the actual compiled source closure"
#endif
#ifndef BM_MODEL16_ADAPTER_SHA256
#error "bind this adapter source"
#endif

namespace bm_model16_snapshot {
namespace sib=string_index_bench::v1;
class Adapter final:public sib::Index {
    std::unique_ptr<bm_m16_tx::Index> index_;
    const bm_m16_tx::Index& built()const {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
public:
    std::size_t verify()const{return built().verify();}
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;
        d.implementation="BM page (16-segment read-only snapshot experiment)";
        d.implementation_version=BM_TX_CORE_SHA256;d.adapter_version=BM_MODEL16_ADAPTER_SHA256;
        d.capabilities.bulk_load=d.capabilities.find=d.capabilities.scan=sib::Support::native;
        d.capabilities.keys.max_bytes=UINT32_MAX;
        d.adaptation_notes="model_commit=" BM_MODEL_COMMIT "; segment_budget=16; scope=read-only-snapshot; "
            "lookup_mode=base; build_mode=bounded-per-page; root_mode=flat; single-threaded; "
            "2048 rows/page, 15/16 slots; insert/update/erase/range_sum unsupported.";
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("second bulk_load is not allowed");
        auto next=std::make_unique<bm_m16_tx::Index>(rows);index_=std::move(next);
    }
    std::optional<sib::Value> find(std::string_view key)const override{return built().find_base(key);}
    sib::InsertResult insert(std::string_view,sib::Value)override {
        throw sib::UnsupportedOperation("snapshot insert unsupported");
    }
    sib::UpdateResult update(std::string_view,sib::Value)override {
        throw sib::UnsupportedOperation("snapshot update unsupported");
    }
    sib::EraseResult erase(std::string_view)override {
        throw sib::UnsupportedOperation("snapshot erase unsupported");
    }
    sib::Value range_sum(std::string_view,std::string_view)const override {
        throw sib::UnsupportedOperation("snapshot range_sum unsupported");
    }
    std::vector<sib::Row> scan(std::string_view lower,std::size_t limit)const override {
        auto rows=built().scan(lower,limit);std::vector<sib::Row> output;output.reserve(rows.size());
        for(auto& row:rows)output.push_back({std::move(row.key),row.value});return output;
    }
    sib::MemoryUsage memory_usage()const override {
        sib::MemoryUsage m;m.index_owned_bytes=index_?index_->memory_bytes():0;
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;
        m.accounting_notes="Snapshot core self-report; excludes allocator and build peak. "
            "Factory acceptance separately records actual requested bytes.";return m;
    }
};
} // namespace bm_model16_snapshot

inline std::unique_ptr<string_index_bench::v1::Index> make_bm_model16_snapshot_index(
        const string_index_bench::v1::Parameters& parameters) {
    if(!parameters.empty())throw std::invalid_argument("the read-only Model16 factory takes no parameters");
    return std::make_unique<bm_model16_snapshot::Adapter>();
}

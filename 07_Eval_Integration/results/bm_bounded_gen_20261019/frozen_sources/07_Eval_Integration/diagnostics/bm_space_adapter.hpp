#pragma once
// Measurement bridge only: preserve the historical PageArm bulk-load copy and find_base path.
#include "bm_page_index.hpp"
#include "index_contract.h"
namespace bm_space {
namespace sib=string_index_bench::v1;
class BaseAdapter final:public sib::Index {
    std::unique_ptr<bm_page::Index> index_;
    void loaded()const {if(!index_)throw std::logic_error("bulk_load required");}
public:
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;d.implementation="BM-page-base";
        d.implementation_version="frozen bm_page_index.hpp; see source SHA ledger";
        d.adapter_version="bm_space_adapter.hpp";
        auto& c=d.capabilities;c.bulk_load=c.find=c.insert=c.update=c.erase=c.scan=sib::Support::native;
        d.adaptation_notes="Owning upstream BlockModel pages; frozen find_base window scan and full-key verification. "
            "bulk_load copies the complete contract-row vector before per-page construction. "
            "scan returns owning keys; page suffix offsets are 32-bit; range_sum unsupported. "
            "Single-threaded root replacement copies O(page count) bindings; no production registration.";
        c.keys.max_bytes=UINT32_MAX;return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("bulk_load once");
        std::vector<static_physical::Row> physical;physical.reserve(rows.size());
        for(const auto& r:rows)physical.push_back({r.key,r.value});
        index_=std::make_unique<bm_page::Index>(physical);
    }
    std::optional<sib::Value> find(std::string_view k)const override {loaded();return index_->find_base(k);}
    sib::InsertResult insert(std::string_view k,sib::Value v)override {
        loaded();return index_->insert(k,v)?sib::InsertResult::inserted:sib::InsertResult::already_exists;
    }
    sib::UpdateResult update(std::string_view k,sib::Value v)override {
        loaded();return index_->update(k,v)?sib::UpdateResult::updated:sib::UpdateResult::not_found;
    }
    sib::EraseResult erase(std::string_view k)override {
        loaded();return index_->erase(k)?sib::EraseResult::erased:sib::EraseResult::not_found;
    }
    std::vector<sib::Row> scan(std::string_view k,std::size_t n)const override {
        loaded();auto rows=index_->scan(k,n);std::vector<sib::Row> out;out.reserve(rows.size());
        for(auto& r:rows)out.push_back({std::move(r.key),r.value});return out;
    }
    sib::Value range_sum(std::string_view,std::string_view)const override {
        throw sib::UnsupportedOperation("range_sum unsupported");
    }
    sib::MemoryUsage memory_usage()const override {
        sib::MemoryUsage m;m.index_owned_bytes=index_?index_->memory_bytes():0;
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;
        m.accounting_notes="Logical self-report excludes allocator and map-node overhead; trace is authoritative.";
        return m;
    }
};
inline std::unique_ptr<sib::Index> make_base(const sib::Parameters& p) {
    if(!p.empty())throw std::invalid_argument("no parameters accepted");
    return std::make_unique<BaseAdapter>();
}
}

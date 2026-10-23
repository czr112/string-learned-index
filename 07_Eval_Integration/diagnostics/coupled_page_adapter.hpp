#pragma once
#include "index_contract.h"
#include "coupled_page_index.hpp"

#ifndef COUPLED_CORE_SHA256
#error "bind the frozen F5 core bundle"
#endif
#ifndef COUPLED_ADAPTER_SHA256
#error "bind this adapter source"
#endif

namespace coupled_adapter {
namespace sib=string_index_bench::v1;
// Experimental v1 bridge. The production factory and its defaults are unchanged.
class Adapter final:public sib::Index {
    std::unique_ptr<coupled_page::Index> index_;
    const coupled_page::Index& built()const {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
    coupled_page::Index& built() {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
public:
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;d.implementation="F5 coupled page (experimental dynamic)";
        d.implementation_version=COUPLED_CORE_SHA256;d.adapter_version=COUPLED_ADAPTER_SHA256;
        auto& c=d.capabilities;
        c.bulk_load=c.find=c.insert=c.update=c.erase=c.scan=sib::Support::native;
        c.keys.max_bytes=UINT32_MAX;
        d.adaptation_notes="Owning F5 page core, fixed 15/16 occupancy; full-key unsigned-byte semantics; "
            "root/leaf replacement has single-threaded strong exception safety; root copying is O(page count). "
            "bulk_load copies contract rows into a temporary core-row vector; scan moves owning keys into contract rows. "
            "Per-page suffix arena/offsets are 32-bit; range_sum unsupported. "
            "Explicit dirty compaction is outside v1 and must be included separately in maintenance measurements.";
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("second bulk_load is not allowed");
        std::vector<coupled_page::Row> copied;copied.reserve(rows.size());
        for(const auto& r:rows)copied.push_back({r.key,r.value});
        auto next=std::make_unique<coupled_page::Index>(copied);index_=std::move(next);
    }
    std::optional<sib::Value> find(std::string_view key)const override{return built().find(key);}
    sib::InsertResult insert(std::string_view key,sib::Value value)override {
        return built().insert(key,value)?sib::InsertResult::inserted:sib::InsertResult::already_exists;
    }
    sib::UpdateResult update(std::string_view key,sib::Value value)override {
        return built().update(key,value)?sib::UpdateResult::updated:sib::UpdateResult::not_found;
    }
    sib::EraseResult erase(std::string_view key)override {
        return built().erase(key)?sib::EraseResult::erased:sib::EraseResult::not_found;
    }
    std::vector<sib::Row> scan(std::string_view lower,std::size_t limit)const override {
        auto rows=built().scan(lower,limit);std::vector<sib::Row> out;out.reserve(rows.size());
        for(auto& r:rows)out.push_back({std::move(r.key),r.value});return out;
    }
    sib::Value range_sum(std::string_view,std::string_view)const override {
        throw sib::UnsupportedOperation("range_sum unsupported");
    }
    sib::MemoryUsage memory_usage()const override {
        sib::MemoryUsage m;m.index_owned_bytes=index_?index_->memory_bytes():0;
        m.adapter_owned_bytes=sizeof(*this);m.retained_input_bytes=0;
        m.accounting_notes="Core object, root, page capacities and dirty iterator payload; adapter object separate. "
            "Excludes map/set node links, allocator metadata, builder scratch, returned scans and complete allocation peak.";
        return m;
    }
    void compact(){built().compact();}
    const coupled_page::Index& core_for_diagnostics()const{return built();}
};
} // namespace coupled_adapter

inline std::unique_ptr<string_index_bench::v1::Index> make_coupled_page_index(
    const string_index_bench::v1::Parameters& parameters) {
    if(!parameters.empty())throw std::invalid_argument("F5 coupled page accepts no parameters");
    return std::make_unique<coupled_adapter::Adapter>();
}

#include "our_static_adapter.h"
#include "static_physical_index.hpp"

#ifndef STATIC_CORE_SHA256
#error "build must bind STATIC_CORE_SHA256 to the actual implementation source"
#endif
#ifndef STATIC_ADAPTER_SHA256
#error "build must bind STATIC_ADAPTER_SHA256 to the adapter source/header digest"
#endif
namespace sib=string_index_bench::v1;
namespace {
template<static_physical::Lookup Policy> class Adapter final:public sib::Index {
    std::unique_ptr<const static_physical::Index> index_;
    const static_physical::Index& built()const {
        if(!index_)throw std::logic_error("bulk_load required before data operations");
        return *index_;
    }
public:
    sib::Descriptor descriptor()const override {
        sib::Descriptor d;d.implementation="StaticPhysicalIndex";
        d.implementation_version=STATIC_CORE_SHA256;d.adapter_version=STATIC_ADAPTER_SHA256;
        d.capabilities.bulk_load=d.capabilities.find=d.capabilities.scan=sib::Support::native;
        d.capabilities.keys.max_bytes=UINT32_MAX;
        d.adaptation_notes="Owning immutable index; one successful build; native owning scan. "
            "Insert/update/erase/range_sum unsupported. Aggregate suffix arena and block count each "
            "limited to UINT32_MAX; overflow throws length_error before publication. Lookup policy=";
        if constexpr(Policy==static_physical::Lookup::learned)d.adaptation_notes+="learned";
        else if constexpr(Policy==static_physical::Lookup::directory)d.adaptation_notes+="directory";
        else d.adaptation_notes+="wide_directory (certificate width >3 only)";
        return d;
    }
    void bulk_load(const std::vector<sib::Row>& rows)override {
        if(index_)throw std::logic_error("second bulk_load is not allowed");
        // Construction failure leaves the fresh adapter unchanged and destructible.
        auto next=std::make_unique<static_physical::Index>(rows);index_=std::move(next);
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
        m.accounting_notes="Core object and vector/string capacities; adapter object separately; "
            "no retained harness input; excludes allocator overhead, RSS, construction peak and returned scan rows.";
        return m;
    }
};
}
std::unique_ptr<sib::Index> make_our_static_index(const sib::Parameters& parameters) {
    std::string policy="learned";bool seen=false;
    for(const auto& p:parameters) {
        if(p.first!="lookup"||seen)throw std::invalid_argument("unknown or duplicate parameter");
        seen=true;policy=p.second;
    }
    if(policy=="learned")return std::make_unique<Adapter<static_physical::Lookup::learned>>();
    if(policy=="directory")return std::make_unique<Adapter<static_physical::Lookup::directory>>();
    if(policy=="wide_directory")return std::make_unique<Adapter<static_physical::Lookup::wide_directory>>();
    throw std::invalid_argument("lookup must be learned, directory or wide_directory");
}

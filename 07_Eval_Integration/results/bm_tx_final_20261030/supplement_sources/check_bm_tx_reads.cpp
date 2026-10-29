// Allocation-only gate through the public factory after complete mutation traces.
#include "bm_tx_adapter.hpp"
#include "bm_tx_trace.hpp"
#include "bm_followup_memory.hpp"
#include <iostream>

namespace sib=string_index_bench::v1;
namespace mem=bm_followup_memory;
struct Probe {std::string key;std::optional<sib::Value> expected;};

static std::vector<Probe> probes(const char* path) {
    bm_tx_trace::Reader reader(path);char magic[8];reader.bytes(magic,8);
    if(std::memcmp(magic,"SIBQRY2\0",8))throw std::runtime_error("query magic");
    reader.integer();reader.integer(); // Source-rank bounds were validated by the query gate.
    const auto groups=reader.integer();
    if(!groups||groups>16)throw std::runtime_error("query group count");
    std::vector<Probe> result;
    for(std::uint64_t group=0;group<groups;++group) {
        reader.key();const auto count=reader.integer();
        if(count>10000)throw std::runtime_error("query count");
        for(std::uint64_t i=0;i<count;++i) {
            auto key=reader.key();std::uint8_t hit;reader.bytes(&hit,1);
            const auto value=reader.integer();reader.integer();
            if(hit>1)throw std::runtime_error("query presence");
            result.push_back({std::move(key),hit?std::optional<sib::Value>(value):std::nullopt});
        }
    }
    reader.finish();return result;
}

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::invalid_argument("rows ops queries");
        const auto rows=bm_tx_trace::rows(argv[1]);
        const auto operations=bm_tx_trace::operations(argv[2]);
        const auto queries=probes(argv[3]);
        if(queries.empty())throw std::runtime_error("no probes");
        mem::begin();auto index=make_bm_tx_experiment_index({});index->bulk_load(rows);
        for(const auto& op:operations)bm_tx_trace::apply(*index,op);
        const auto& concrete=dynamic_cast<const bm_tx_experiment::Adapter&>(*index);
        concrete.verify();const auto splits=concrete.maintenance().tx_splits;
        // Count every allocation attempt, including over-aligned/nothrow new variants.
        mem::arm(0);
        for(const auto& query:queries)
            if(index->find(query.key)!=query.expected)throw std::runtime_error("query answer");
        const auto allocations=mem::disarm();
        if(allocations)throw std::runtime_error("find allocated");
        index.reset();mem::tracking=false;
        if(mem::live)throw std::runtime_error("destruction leaked");
        std::cout<<"{\"queries\":"<<queries.size()<<",\"allocations\":"<<allocations
                 <<",\"tx_splits\":"<<splits<<",\"mismatches\":0,\"live_after_destroy\":0}\n";
    }catch(const std::exception& error) {
        mem::disarm();std::cerr<<error.what()<<'\n';return 1;
    }
}

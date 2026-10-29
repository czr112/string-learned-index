#pragma once
// Diagnostic-only global counters; no fields are added to the measured Page.
#include BM_DIRECTORY_MEMORY_HEADER
#include <chrono>
#include <cstdint>

namespace bm_directory_metrics {
struct Counters {
    std::uint64_t events=0,block_objects=0,fence_objects=0,object_bytes=0;
    std::uint64_t allocations=0,allocated_bytes=0,wall_ns=0;
};
inline Counters counters;
class Timer {
    using Clock=std::chrono::steady_clock;
    Clock::time_point start_;
    std::uint64_t requests_,bytes_;
public:
    Timer(std::size_t count,std::size_t block_size,std::size_t fence_size):
        start_(Clock::now()),requests_(bm_followup_memory::requests),bytes_(bm_followup_memory::requested_bytes) {
        ++counters.events;counters.block_objects+=count+1;counters.fence_objects+=count+1;
        counters.object_bytes+=(count+1)*(block_size+fence_size);
    }
    ~Timer() noexcept {
        counters.allocations+=bm_followup_memory::requests-requests_;
        counters.allocated_bytes+=bm_followup_memory::requested_bytes-bytes_;
        counters.wall_ns+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start_).count());
    }
};
}

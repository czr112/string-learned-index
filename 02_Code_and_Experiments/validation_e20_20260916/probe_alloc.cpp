// Small probe: how many allocations does each statistics entry point make?
// Written because bench_e20_diag reported identical allocation counts for R1 and
// R2, which contradicts "R2 allocates one id array per region".
#include "data.hpp"
#include "index.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void* __real_aligned_alloc(std::size_t, std::size_t);
extern "C" int __real_posix_memalign(void**, std::size_t, std::size_t);
extern "C" void __real_free(void*);

namespace n {
inline std::uint64_t calls = 0;
inline long long live = 0;
}  // namespace n

extern "C" void* __wrap_malloc(std::size_t s) { ++n::calls; void* p = __real_malloc(s); n::live += (long long)s; return p; }
extern "C" void* __wrap_calloc(std::size_t a, std::size_t b) { ++n::calls; void* p = __real_calloc(a, b); n::live += (long long)a * b; return p; }
extern "C" void* __wrap_realloc(void* q, std::size_t s) { ++n::calls; void* p = __real_realloc(q, s); n::live += (long long)s; return p; }
extern "C" void* __wrap_aligned_alloc(std::size_t a, std::size_t s) { ++n::calls; void* p = __real_aligned_alloc(a, s); n::live += (long long)s; return p; }
extern "C" int __wrap_posix_memalign(void** o, std::size_t a, std::size_t s) { ++n::calls; int r = __real_posix_memalign(o, a, s); n::live += (long long)s; return r; }
extern "C" void __wrap_free(void* p) { __real_free(p); }

using namespace validation;

int main() {
    const std::string kind = "mixed";
    const auto keys = make_data(kind, 131072, "");
    Config c;
    c.block_bytes = 1024;
    c.slots = 16;
    c.model_kind = ModelKind::Context16;

    Index so(c);

    n::calls = 0;
    auto d = so.demand_profile(keys);
    std::size_t regions = d.size();
    const auto calls_plain = n::calls;

    n::calls = 0;
    auto ctx = so.demand_profile_ctx(keys);
    const auto calls_ctx = n::calls;

    std::size_t ids_total = 0;
    for (const auto& rd : ctx) ids_total += rd.group_ids.size();

    std::cout << "regions=" << regions
              << " allocs(demand_profile)=" << calls_plain
              << " allocs(demand_profile_ctx)=" << calls_ctx
              << " delta=" << (static_cast<long long>(calls_ctx) - static_cast<long long>(calls_plain))
              << " ids_total=" << ids_total << std::endl;
    return 0;
}

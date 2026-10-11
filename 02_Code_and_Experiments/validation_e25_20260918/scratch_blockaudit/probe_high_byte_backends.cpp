// Isolation probe: high-byte keys, one backend at a time, small fixtures.
#include "data.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
using namespace validation;

static std::string hx(const std::string& k) {
    std::string o; char buf[8];
    for (unsigned char c : k) { std::snprintf(buf, sizeof buf, "%02x", c); o += buf; }
    return o;
}

static void run_backend(ModelBackend b, const char* name) {
    std::vector<Pair> keys;
    std::uint64_t v = 1;
    for (unsigned x : {1u, 0x40u, 0x7fu, 0x80u, 0x81u, 0xbfu, 0xc0u, 0xffu})
        keys.emplace_back(std::string("k") + std::string(1, (char)x), v++);
    Config c = e25sw::baseline_config(32);
    c.model_backend = b;
    Index idx(c);
    idx.bulk_load(keys);
    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
    std::size_t bad = 0;
    std::printf("%s: size=%zu regions=%zu\n", name, idx.size(), idx.region_count());
    for (const auto& kv : keys) {
        const auto r = idx.find(kv.first, qmode);
        if (!r.found || r.value != kv.second) {
            std::printf("   MISS %s want=%llu found=%d value=%llu\n", hx(kv.first).c_str(),
                        (unsigned long long)kv.second, (int)r.found, (unsigned long long)r.value);
            ++bad;
        }
    }
    std::printf("%s: bad=%zu\n", name, bad);
}

int main() {
    run_backend(ModelBackend::Context16, "Context16");
    run_backend(ModelBackend::EqualDepthDir, "EqualDepthDir");
    run_backend(ModelBackend::EqualDepthDirTrunc, "EqualDepthDirTrunc");
    return 0;
}

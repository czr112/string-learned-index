// Debug probe: which keys of the order gate's index fixture fail, and why?
#include "data.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
using namespace validation;

static std::string byte_str(unsigned b) { return std::string(1, static_cast<char>(b)); }

static std::vector<Pair> byte_domain_keys() {
    std::vector<Pair> out;
    std::uint64_t v = 1;
    auto add = [&out, &v](std::string k) { out.emplace_back(std::move(k), v++); };
    for (unsigned b = 0; b < 256; ++b) add(std::string("k") + byte_str(b));
    for (unsigned b : {0u, 0x7fu, 0x80u, 0xffu}) {
        add(std::string("k") + byte_str(b) + "x");
        add(std::string("k") + byte_str(b) + std::string(1, '\0'));
    }
    out.emplace_back(std::string(), v++);
    out.emplace_back(std::string(1, '\0'), v++);
    out.emplace_back(std::string("\0\0", 2), v++);
    for (unsigned b = 0x70; b <= 0x90; ++b) add(std::string("pfx") + byte_str(b));
    return out;
}

static std::string hexkey(const std::string& k) {
    std::string o;
    char buf[8];
    for (unsigned char c : k) { std::snprintf(buf, sizeof buf, "%02x", c); o += buf; }
    return o.empty() ? std::string("<empty>") : o;
}

int main(int argc, char** argv) {
    const std::vector<Pair> input = byte_domain_keys();
    Config c = e25sw::baseline_config(32);
    c.model_backend = (argc > 1 && std::string(argv[1]) == "c16") ? ModelBackend::Context16 : ModelBackend::EqualDepthDirTrunc;
    c.multiblock = true;
    c.reuse_group_ids = true;
    c.region_keys = 64;
    Index idx(c);
    idx.bulk_load(input);
    const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};
    std::printf("input=%zu size=%zu regions=%zu\n", input.size(), idx.size(), idx.region_count());
    std::size_t bad = 0;
    for (const auto& kv : input) {
        const auto r = idx.find(kv.first, qmode);
        const auto loc = idx.locate(kv.first);
        if (!r.found || r.value != kv.second) {
            std::printf("  MISS key=%s want=%llu found=%d value=%llu locate=%llu\n",
                        hexkey(kv.first).c_str(), (unsigned long long)kv.second, (int)r.found,
                        (unsigned long long)r.value, (unsigned long long)loc);
            if (++bad > 12) break;
        }
    }
    std::printf("bad=%zu\n", bad);
    for (std::size_t i = 0; i < idx.region_count() && i < 3; ++i) {
        const auto& reg = idx.region(i);
        std::printf("region %zu: prefix_len=%zu groups=%u size=%zu\n", i, reg.prefix_length(),
                    reg.groups(), reg.size());
    }
    return 0;
}


// Minimal reproduction probe: does a multi-block layout with a SMALL suffix area
// (block 1024, 48 slots -> 192 B per sub-block) keep range scan correct?
// Compiled against both the E10 and the E11 headers; identical failure in both
// means the defect predates E11.

#include "data.hpp"

#include <iostream>
#include <map>
#include <random>
#include <set>

using namespace validation;

using Reference = std::map<std::string, std::uint64_t, Less>;

static std::vector<Pair> expected_scan(const Reference& r, std::string_view key, std::size_t n) {
    std::vector<Pair> out;
    for (auto it = r.lower_bound(std::string(key)); it != r.end() && out.size() < n; ++it)
        out.push_back(*it);
    return out;
}

int main() {
    std::mt19937_64 rng(20260916);
    std::set<std::string, Less> pool_set;
    for (const auto& s : std::vector<std::string>{
             "", "a", "aa", "aaa", "z",
             std::string("a\0b", 3), std::string("\0", 1), std::string("\xff", 1),
             std::string(8, '\0'), std::string(8, '\xff'), std::string(4096, 'q'),
         })
        pool_set.insert(s);
    for (int i = 0; i < 2200; ++i) {
        std::string s = i % 3 ? "common/prefix/path/" : "";
        for (std::size_t j = 0, m = rng() % 100; j < m; ++j)
            s.push_back(static_cast<char>(rng() % 256));
        pool_set.insert(s);
    }
    const std::vector<std::string> pool(pool_set.begin(), pool_set.end());

    Reference ref;
    std::vector<Pair> initial;
    for (const auto& k : pool) {
        initial.emplace_back(k, ref.size() + 1);
        ref[k] = initial.back().second;
    }

    Config c;
    c.block_bytes = 1024;
    c.slots = 48;
    c.model_kind = ModelKind::Context16;
    c.region_keys = 64;
    c.multiblock = true;

    Index idx(c);
    idx.bulk_load(initial);
    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const std::size_t G = idx.region(i).slot_table().size();
        idx.rebuild_with_caps(i, idx.region(i).all(),
                              std::vector<std::uint16_t>(G, 48),
                              std::vector<std::uint32_t>(G, 1024));
    }

    std::size_t multi = 0, maxnb = 0;
    for (std::size_t i = 0; i < idx.region_count(); ++i)
        for (std::uint32_t g = 0; g < idx.region(i).groups(); ++g) {
            const auto nb = idx.region(i).subblock_count(g);
            if (nb > 1) ++multi;
            maxnb = std::max(maxnb, nb);
        }
    std::cerr << "regions=" << idx.region_count() << " multiblock_groups=" << multi
              << " max_subblocks=" << maxnb << "\n";

    // Static scan sweep first: every pool key as a start, small limits.
    for (const auto& k : pool) {
        for (std::size_t n : {std::size_t(1), std::size_t(10)}) {
            const auto got = idx.scan(k, n);
            const auto want = expected_scan(ref, k, n);
            if (got != want) {
                std::cerr << "STATIC SCAN MISMATCH start_len=" << k.size() << " n=" << n
                          << " got=" << got.size() << " want=" << want.size() << "\n";
                for (std::size_t x = 0; x < std::min(got.size(), want.size()); ++x)
                    if (got[x] != want[x]) {
                        std::cerr << "  first diff at " << x
                                  << " got_len=" << got[x].first.size()
                                  << " want_len=" << want[x].first.size() << "\n";
                        break;
                    }
                return 1;
            }
        }
    }
    std::cerr << "static scan sweep OK\n";

    for (std::size_t step = 0; step < 6000; ++step) {
        const auto& key = pool[rng() % pool.size()];
        const std::uint64_t value = step % 7 ? rng() : 0;
        switch (rng() % 6) {
            case 0: ref.emplace(key, value); idx.insert(key, value); break;
            case 1: { auto it = ref.find(key); if (it != ref.end()) it->second = value; idx.update(key, value); break; }
            case 2: ref.erase(key); idx.erase(key); break;
            case 3: {
                const auto n = rng() % 30;
                const auto got = idx.scan(key, n);
                const auto want = expected_scan(ref, key, n);
                if (got != want) {
                    std::cerr << "DYNAMIC SCAN MISMATCH step=" << step << " start_len=" << key.size()
                              << " n=" << n << " got=" << got.size() << " want=" << want.size() << "\n";
                    for (std::size_t x = 0; x < std::min(got.size(), want.size()); ++x)
                        if (got[x] != want[x]) {
                            std::cerr << "  first diff at " << x
                                      << " got_len=" << got[x].first.size()
                                      << " want_len=" << want[x].first.size() << "\n";
                            break;
                        }
                    return 1;
                }
                break;
            }
            default: {
                const auto got = idx.find(key, Mode{Route::Interval, Prefetch::Off, true, true,
                                                    OvSearch::Hybrid, 128});
                const auto it = ref.find(key);
                if (got.found != (it != ref.end()) || (got.found && got.value != it->second)) {
                    std::cerr << "FIND MISMATCH step=" << step << "\n";
                    return 1;
                }
                break;
            }
        }
        if (idx.size() != ref.size()) {
            std::cerr << "SIZE MISMATCH step=" << step << "\n";
            return 1;
        }
        if (step % 499 == 0) {
            const auto got = idx.scan({}, ref.size() + 1);
            const auto want = expected_scan(ref, {}, ref.size() + 1);
            if (got != want) {
                std::cerr << "FULL SCAN MISMATCH step=" << step
                          << " got=" << got.size() << " want=" << want.size() << "\n";
                return 1;
            }
        }
    }

    std::cerr << "ALL OK\n";
    return 0;
}

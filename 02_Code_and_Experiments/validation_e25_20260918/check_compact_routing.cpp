// Gate for the compact region-routing candidate.
//
// WHY THIS EXISTS AS A GATE AND NOT AS AN ARGUMENT. The candidate replaces locate()'s binary
// search with one that walks an 8-byte digest array. The claim that the two are equivalent
// rests on the digest being order-preserving and on ties falling back to the full comparison.
// That is a good argument and it is not evidence. This program compares the two searches on
// EVERY key, on the same index state, and fails on the first disagreement -- including the
// cases where the argument is easiest to get wrong: the empty key, an embedded NUL, a byte
// >= 0x80, a shared prefix longer than the 8 bytes the digest can see, and deliberate digest
// collisions.
//
// IT ALSO EXERCISES THE INVALIDATION PATH, which is the whole difference between the
// diagnostics prototype and an integrated candidate. The prototype borrowed its fences from
// immutable input and never had to answer when the digests go stale. Here the digests are
// derived from nodes_ and keyed to structure_version_, so a build, a rebuild or a split must
// invalidate them and the check is repeated AFTER a replacement run that forces all three.
//
// AND IT CHECKS THE OBSERVABLE, NOT JUST THE ROUTE. A routing change that returned the same
// region but a different answer would pass a locate-only check, so find() and scan() results
// are compared between a switch-off and a switch-on index over the same data.
//
// Usage:  check_compact_routing KIND N [TRANCOCSV]

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace validation;

#ifndef E25_DIAG
#error "check_compact_routing must be built with -DE25_DIAG"
#endif

namespace {

std::uint64_t checks = 0;
std::uint64_t failures = 0;

void require(bool condition, const std::string& what) {
    ++checks;

    if (!condition) {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

Config plain_config(std::uint32_t thr) {
    Config c = e25sw::baseline_config(thr);
    e25sw::pin_frozen_switches(c);
    c.compact_region_routing = false;

    return c;
}

// Compares the two searches on one key and reports the first disagreement with the key's
// bytes, because "they differ" without the key is not actionable.
void compare_one(const Index& index, std::string_view key, const std::string& label) {
    const std::size_t scanned = index.diag_locate_scanned(key);
    const std::size_t routed = index.diag_locate_routed(key);

    if (scanned != routed) {
        ++checks;
        ++failures;
        std::cout << "FAIL: routing disagrees on " << label << " len=" << key.size()
                  << " scanned=" << scanned << " routed=" << routed << "\n";
    } else {
        ++checks;
    }
}

// Every key, plus probes that sit strictly between keys, before the first and after the last.
// The between-probes matter because a fence is a region's FIRST key, so the boundary cases
// are keys that do not exist.
void sweep(const Index& index, const std::vector<Pair>& keys, const std::string& label) {
    for (const auto& kv : keys) {
        compare_one(index, kv.first, label + "/key");

        std::string just_below = kv.first;
        just_below.push_back('\x01');
        compare_one(index, just_below, label + "/key+1");

        std::string truncated = kv.first;
        truncated.resize(truncated.size() / 2);
        compare_one(index, truncated, label + "/prefix-of-key");
    }

    compare_one(index, std::string(), label + "/empty");
    compare_one(index, std::string("\x00", 1), label + "/nul");
    compare_one(index, std::string("\xff\xff\xff\xff", 4), label + "/high");
    compare_one(index, std::string(4096, 'z'), label + "/very-long");

    // A key below everything and one above everything, in unsigned-byte order.
    compare_one(index, std::string("\x01", 1), label + "/below-all");
    compare_one(index, std::string("\xff\xff\xff\xff\xff\xff\xff\xff\xff", 9), label + "/above-all");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_compact_routing KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        const auto keys = make_data(kind, nkeys, trcsv);

        Config off = plain_config(32);
        Config on = off;
        on.compact_region_routing = true;

        // ------------------------------------------------------------------ A. build state
        auto a = std::make_unique<Index>(off);
        auto b = std::make_unique<Index>(on);
        a->bulk_load(keys);
        b->bulk_load(keys);

        require(a->region_count() == b->region_count(), "the switch does not change the region count");

        // n == 1 is a real case (the empty index has one region) and the binary search in
        // the routed path must return 0 there too.
        {
            auto empty_a = std::make_unique<Index>(off);
            auto empty_b = std::make_unique<Index>(on);
            empty_a->bulk_load({});
            empty_b->bulk_load({});
            compare_one(*empty_b, std::string("anything"), "empty-index");
        }

        sweep(*b, keys, "bulk");

        // ------------------------------------------------------------------ B. digest stress
        // A key set whose fences deliberately collide in their first 8 bytes: every key here
        // shares a long prefix, so after the shared-prefix skip every digest is identical and
        // EVERY comparison must come from the full-key fallback.
        {
            std::vector<Pair> colliding;
            const std::string shared(64, 'p');

            for (int i = 0; i < 40; ++i) {
                colliding.emplace_back(shared + "aaaaaaaa" + static_cast<char>('a' + i % 26)
                                                   + std::to_string(i),
                                       static_cast<std::uint64_t>(i));
            }

            std::sort(colliding.begin(), colliding.end(),
                      [](const Pair& x, const Pair& y) { return compare(x.first, y.first) < 0; });

            Config off2 = plain_config(8);
            Config on2 = off2;
            on2.compact_region_routing = true;
            off2.region_keys = 4;
            on2.region_keys = 4;

            auto ca = std::make_unique<Index>(off2);
            auto cb = std::make_unique<Index>(on2);
            ca->bulk_load(colliding);
            cb->bulk_load(colliding);

            require(ca->region_count() > 1, "the collision set really produced several regions");
            sweep(*cb, colliding, "collision");

            // And the observable must match, not just the route.
            for (const auto& kv : colliding) {
                require(ca->find(kv.first, Mode{Route::Interval, Prefetch::Off, true, true,
                                               OvSearch::Hybrid, 8, false})
                            .value ==
                            cb->find(kv.first, Mode{Route::Interval, Prefetch::Off, true, true,
                                                   OvSearch::Hybrid, 8, false})
                                .value,
                        "collision set: the two indexes answer the same");
            }
        }

        // ------------------------------------------------------------------ C. after mutation
        // Replacement forces rebuilds and, past the split threshold, region splits -- the
        // three events that move structure_version_ and must invalidate the digests.
        {
            std::deque<std::string> live;
            std::unordered_set<std::string> live_set;

            for (const auto& kv : keys) {
                live.push_back(kv.first);
                live_set.insert(kv.first);
            }

            std::uint64_t serial = 0;
            const std::size_t rounds = std::min<std::size_t>(keys.size(), 40000);

            for (std::size_t i = 0; i < rounds; ++i) {
                const std::string victim = live.front();
                live.pop_front();
                live_set.erase(victim);

                std::string fresh = victim + "~c" + std::to_string(serial++);

                while (!live_set.insert(fresh).second) fresh += "x";

                live.push_back(fresh);
                a->erase(victim);
                b->erase(victim);
                a->insert(fresh, 0x1000ULL + serial);
                b->insert(fresh, 0x1000ULL + serial);
            }

            require(a->size() == b->size(), "the two indexes hold the same number of keys after mutation");
            require(a->structure_version() == b->structure_version(),
                    "the two indexes went through the same structural changes");

            sweep(*b, keys, "after-replacement");

            for (const auto& name : live) {
                compare_one(*b, name, "after-replacement/live");
            }
        }

        // ------------------------------------------------------------------ D. observable
        // Same data, same queries, two configurations. The routing change is only acceptable
        // if it is invisible from outside.
        {
            const auto queries = make_queries(keys, 4000, 20261009, false);

            for (const auto& q : queries) {
                const Result ra = a->find(q.key, Mode{Route::Interval, Prefetch::Off, true, true,
                                                     OvSearch::Hybrid, 32, false});
                const Result rb = b->find(q.key, Mode{Route::Interval, Prefetch::Off, true, true,
                                                     OvSearch::Hybrid, 32, false});
                require(ra.found == rb.found, "find agrees on hit/miss");
                require(!ra.found || ra.value == rb.value, "find agrees on the value");
            }

            for (std::size_t i = 0; i < keys.size(); i += std::max<std::size_t>(1, keys.size() / 64)) {
                const auto sa = a->scan(keys[i].first, 37);
                const auto sb = b->scan(keys[i].first, 37);
                require(sa.size() == sb.size(), "scan agrees on the row count");

                for (std::size_t j = 0; j < std::min(sa.size(), sb.size()); ++j) {
                    require(sa[j].first == sb[j].first && sa[j].second == sb[j].second,
                            "scan agrees row by row");
                }
            }
        }

        // ------------------------------------------------------------------ report
        std::cout << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
                  << ",\"regions\":" << b->region_count()
                  << ",\"routing_regions\":" << b->routing_regions()
                  << ",\"routing_bytes\":" << b->routing_bytes()
                  << ",\"checks\":" << checks
                  << ",\"failures\":" << failures
                  << "}" << std::endl;

        if (failures != 0) {
            std::cout << failures << " FAILED out of " << checks << "\n";
            return 1;
        }

        std::cout << checks << "/" << checks << " checks passed" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

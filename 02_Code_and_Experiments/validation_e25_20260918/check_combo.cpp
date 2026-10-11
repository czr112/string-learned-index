// Gate for the WIDE x compact-region-routing COMBINATION: four arms, one index state each,
// every answer checked against a reference AND against each other, and both mechanisms must
// be shown to have actually run.
//
// THE QUESTION THIS ANSWERS BEFORE ANY TIMER RUNS. WIDE (subblock_reclaim + reclaim_widest)
// was accepted as a scenario candidate from G3/G5; the compact region routing was accepted as
// a default-off short-key navigation candidate from its own round. Neither round measured
// them WITH each other. A timing sweep over B/W/C/WC is only worth its machine time if the
// four configurations provably answer the same on the same state and if the branches the
// round claims to study are reachable on the load it will run. This gate is that proof.
//
// FOUR ARMS. B = shipped configuration. W = B + subblock_reclaim + reclaim_widest (the
// accepted WIDE pair, not re-split here). C = B + compact_region_routing. WC = both. Every
// pair's exact switch difference is asserted with require_only_switches_differ, so an arm
// that silently became a copy of another fails instead of passing a comparison that never
// happened.
//
// ONE BACKEND PER RUN. G5 showed the WIDE benefit is strongly backend-dependent, so the
// Context16 and LitsHpt runs are separate invocations and no ratio is ever pooled across
// them. The gate must still be run for BOTH, because "the widened branch runs on this load"
// is a different claim per backend.
//
// WHAT IS CHECKED, AND WHY EACH ONE IS HERE:
//   * every key, plus probes strictly between keys, plus empty / NUL / high-byte / 4,096-byte
//     keys: the compact routing must equal the scanned search on EVERY one
//     (diag_locate_scanned vs diag_locate_routed), on the C and WC arms;
//   * a deliberate digest-collision set (64-byte shared prefix, several regions) where every
//     comparison must fall back to the full key;
//   * full replacement out to 2x N with the live count held constant, in the same same/grow
//     key discipline the timing rounds use: after every checkpoint ALL FOUR arms' full
//     contents are compared element by element against each other and against a std::map
//     reference. Agreeing on a wrong answer is not a pass;
//   * delete-and-reinsert and duplicate-insert on all four arms;
//   * THE LAZY DIRECTORY REBUILD IS SHOWN TO RUN: the digests are empty before the first
//     lookup, filled by it, then a forced region split must leave them STALE (fewer entries
//     than regions, because the write path must not rebuild) and the next lookup must
//     rebuild them. Without this the invalidation path would only be argued, not observed;
//   * the WIDE branch counters must be nonzero on this load (subblock_reclaims and
//     reclaim_widened) for the arms that have the switches on. If they are zero the gate
//     exits 3 NOT EXERCISED: the load never reached the branch, so a timing run would be
//     certifying nothing about it.
//
// Usage:  check_combo KIND N [TRANCOCSV] [BACKEND]

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

#ifndef E25_DIAG
#error "check_combo must be built with -DE25_DIAG: it asserts that the widened branch ran"
#endif

namespace {

std::uint64_t checks = 0;
std::uint64_t failures = 0;

void require(bool ok, const std::string& what) {
    ++checks;

    if (!ok) {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

const char* ARM_NAMES[4] = {"B", "W", "C", "WC"};

struct Pipeline {
    std::vector<Alloc> s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};

Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
    Pipeline p;

    for (const auto& gs : demand) {
        const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
        const Alloc d = demand_single(gs, budget);
        StrictLog log;
        std::vector<std::uint16_t> caps;
        p.s_alloc.push_back(demand_strict(gs, d, log, caps));
        p.s_caps.push_back(std::move(caps));
    }

    return p;
}

std::vector<std::vector<std::uint16_t>> sg_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint16_t>> o;

    for (const auto& a : v) o.push_back(a.sg);

    return o;
}

std::vector<std::vector<std::uint32_t>> bb_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint32_t>> o;

    for (const auto& a : v) o.push_back(a.bb);

    return o;
}

void assemble_arms(Config cfg[4], std::uint32_t thr, ModelBackend backend) {
    for (int a = 0; a < 4; ++a) {
        cfg[a] = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg[a]);
        cfg[a].model_backend = backend;
    }

    cfg[1].subblock_reclaim = true;
    cfg[1].reclaim_widest = true;
    cfg[2].compact_region_routing = true;
    cfg[3].subblock_reclaim = true;
    cfg[3].reclaim_widest = true;
    cfg[3].compact_region_routing = true;

    // Every claimed difference, asserted. The pairs are chosen so each single mechanism and
    // the combination are separated by their own named switch set.
    e25sw::require_only_switches_differ(cfg[0], cfg[2], {"compact_region_routing"});
    e25sw::require_only_switches_differ(cfg[0], cfg[1], {"subblock_reclaim", "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[1], cfg[3], {"compact_region_routing"});
    e25sw::require_only_switches_differ(cfg[2], cfg[3], {"subblock_reclaim", "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[0], cfg[3],
                                        {"compact_region_routing", "subblock_reclaim",
                                         "reclaim_widest"});
    e25sw::require_only_switches_differ(cfg[1], cfg[2],
                                        {"compact_region_routing", "subblock_reclaim",
                                         "reclaim_widest"});
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
    compare_one(index, std::string("\x01", 1), label + "/below-all");
    compare_one(index, std::string("\xff\xff\xff\xff\xff\xff\xff\xff\xff", 9), label + "/above-all");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_combo KIND N [TRANCOCSV] [BACKEND]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string backend_arg = argc > 4 ? argv[4] : "ctx16";
        const std::string mode_arg = argc > 5 ? argv[5] : "same";
        const ModelBackend backend =
            backend_arg == "lits" ? ModelBackend::LitsHpt : ModelBackend::Context16;
        const std::string backend_name =
            backend == ModelBackend::LitsHpt ? "LitsHpt" : "Context16";

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);

            if (!fz) throw std::runtime_error("frozen file missing");

            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");

            if (p == std::string::npos) throw std::runtime_error("frozen field missing");

            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        Config cfg[4];
        assemble_arms(cfg, thr, backend);

        // One build plan, made once with the B configuration and reused for all four arms:
        // the capacity table is an INPUT to the comparison, not part of any arm, so an arm
        // that derived its own would silently compare different data layouts.
        Pipeline plan;
        {
            Index probe(cfg[0]);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;

            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());

            plan = make_plan(demand);
        }

        std::unique_ptr<Index> idx[4];

        for (int a = 0; a < 4; ++a) {
            idx[a] = std::make_unique<Index>(cfg[a]);
            idx[a]->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc),
                                        plan.s_caps);
        }

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        // ---------------------------------------------------------------- A. bulk state
        for (int a = 1; a < 4; ++a)
            require(idx[a]->region_count() == idx[0]->region_count(),
                    "the arm does not change the region count");

        // The routed search must equal the scanned search on EVERY key for the arms that
        // actually route through the digests.
        sweep(*idx[2], keys, "bulk/C");
        sweep(*idx[3], keys, "bulk/WC");

        // The four arms must answer the same on the same state: point checks over the whole
        // key set, against each other and against the input value.
        {
            std::size_t bad = 0;

            for (const auto& kv : keys) {
                const Result r0 = idx[0]->find(kv.first, qmode);

                if (!r0.found || r0.value != kv.second) {
                    if (bad < 5) std::cout << "FAIL: B on bulk state, key=" << kv.first << "\n";

                    ++bad;
                }

                for (int a = 1; a < 4; ++a) {
                    const Result r = idx[a]->find(kv.first, qmode);

                    if (r.found != r0.found || (r0.found && r.value != r0.value)) {
                        if (bad < 5)
                            std::cout << "FAIL: arm " << ARM_NAMES[a] << " disagrees on "
                                      << kv.first << "\n";

                        ++bad;
                    }
                }
            }

            require(bad == 0, "bulk state: all four arms find every key with the right value");
        }

        // -------------------------------------------------- digest-collision stress
        // A key set whose fences deliberately collide in their first 8 bytes: every key
        // shares a long prefix, so after the shared-prefix skip every digest is identical and
        // EVERY comparison must come from the full-key fallback.
        {
            std::vector<Pair> colliding;
            const std::string shared(64, 'p');

            for (int i = 0; i < 40; ++i) {
                colliding.emplace_back(shared + "aaaaaaaa" + static_cast<char>('a' + i % 26) +
                                           std::to_string(i),
                                       static_cast<std::uint64_t>(i));
            }

            std::sort(colliding.begin(), colliding.end(),
                      [](const Pair& x, const Pair& y) { return compare(x.first, y.first) < 0; });

            Config small[4];
            assemble_arms(small, 8, backend);

            for (int a = 0; a < 4; ++a) small[a].region_keys = 4;

            std::unique_ptr<Index> cidx[4];

            for (int a = 0; a < 4; ++a) {
                cidx[a] = std::make_unique<Index>(small[a]);
                cidx[a]->bulk_load(colliding);
            }

            // region_keys = 4 with 40 keys must produce several regions, or the digest
            // array has nothing to disambiguate and this block verifies nothing.
            require(cidx[2]->region_count() > 1,
                    "the collision set really produced several regions");

            sweep(*cidx[2], colliding, "collision/C");
            sweep(*cidx[3], colliding, "collision/WC");

            const Mode cmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 8,
                             false};
            std::size_t bad = 0;

            for (const auto& kv : colliding) {
                const Result r0 = cidx[0]->find(kv.first, cmode);

                if (!r0.found || r0.value != kv.second) ++bad;

                for (int a = 1; a < 4; ++a) {
                    const Result r = cidx[a]->find(kv.first, cmode);

                    if (r.found != r0.found || (r0.found && r.value != r0.value)) ++bad;
                }
            }

            require(bad == 0, "collision set: all four arms return the right value");
        }

        // -------------------------------------------------------- replacement, to 2xN
        const std::string mode = mode_arg.empty() ? "same" : mode_arg;
        const bool grow = (mode == "grow");
        if (mode != "same" && mode != "grow")
            throw std::invalid_argument("MODE must be same or grow");
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        std::deque<std::string> live;
        std::unordered_set<std::string> live_set;
        Reference ref;

        for (const auto& kv : keys) {
            live.push_back(kv.first);
            live_set.insert(kv.first);
            ref[kv.first] = kv.second;
        }

        std::size_t done = 0;
        std::uint64_t serial = 0;
        std::uint64_t reclaims_seen = 0, widened_seen = 0;

        for (double frac : CPS) {
            const std::size_t target = static_cast<std::size_t>(double(N) * frac);

            if (target <= done) continue;

            struct Move {
                std::string victim, fresh;
                std::uint64_t value;
            };

            std::vector<Move> seg;
            seg.reserve(target - done);

            for (std::size_t i = done; i < target; ++i) {
                const std::string victim = live.front();
                live.pop_front();
                live_set.erase(victim);

                std::string base = victim;
                const auto pos = base.find("~r");

                if (pos != std::string::npos) base.resize(pos);

                std::string fresh;

                if (grow) {
                    fresh = base + "~r" + std::to_string(serial);
                } else {
                    // Same length, same anchor: change one byte until the key is one the
                    // reference does not already hold. This keeps the key-length
                    // distribution exactly as planned.
                    fresh = base;
                    bool ok = false;

                    for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                        fresh.back() = static_cast<char>('a' + ((serial + t) % 26));

                        if (fresh != victim && ref.find(fresh) == ref.end() &&
                            live_set.find(fresh) == live_set.end()) {
                            ok = true;
                            break;
                        }
                    }

                    if (!ok)
                        throw std::runtime_error("same-length replacement exhausted its alphabet");
                }

                const std::uint64_t val = 0x7000ULL + serial;
                ++serial;
                seg.push_back({victim, fresh, val});
                live.push_back(fresh);
                live_set.insert(fresh);
            }

            for (const auto& m : seg) {
                if (ref.erase(m.victim) != 1) throw std::runtime_error("erase target absent");

                if (ref.find(m.fresh) != ref.end())
                    throw std::runtime_error("insert target present");

                ref[m.fresh] = m.value;
            }

            // ERASE AND INSERT ARE INTERLEAVED, one replacement at a time -- the shape
            // check_widest and bench_widest both use. A first version of this gate batched
            // all erases before all inserts, and the batch made the load DISAPPEARLY mild:
            // deleting a quarter of the keys first leaves the primary blocks full of holes,
            // so the insertions mostly place without ever reaching the reclaim, and on
            // mixed/skew `reclaim_widened` never ran at all. Interleaved, each victim's slot
            // is immediately taken by its own replacement, which is what makes the reclaim
            // path -- and therefore the widening question -- real on this load.
            for (int a = 0; a < 4; ++a) {
                for (const auto& m : seg) {
                    idx[a]->erase(m.victim);
                    idx[a]->insert(m.fresh, m.value);
                }
            }

            for (int a = 0; a < 4; ++a)
                require(idx[a]->size() == ref.size(), "live count matches the reference");

            // Full content, element by element, four arms and the reference. Agreeing on a
            // wrong answer is not a pass, so the reference comparison is part of the same
            // loop and not a separate checksum.
            std::vector<Pair> contents[4];
            bool sizes_ok = true;

            for (int a = 0; a < 4; ++a) {
                contents[a] = idx[a]->scan({}, idx[a]->size());
                sizes_ok = sizes_ok && contents[a].size() == ref.size();
            }

            require(sizes_ok, "four arms and the reference agree on the row count");

            if (sizes_ok) {
                std::size_t bad = 0;
                auto rit = ref.begin();

                for (std::size_t i = 0; i < contents[0].size() && rit != ref.end(); ++i, ++rit) {
                    const auto& row0 = contents[0][i];
                    bool same = true;

                    for (int a = 1; a < 4; ++a)
                        same = same && contents[a][i].first == row0.first &&
                               contents[a][i].second == row0.second;

                    if (!(same && rit->first == row0.first && rit->second == row0.second)) {
                        if (bad < 5)
                            std::cout << "FAIL: content diverges at row " << i << " key="
                                      << row0.first << "\n";

                        ++bad;
                    }
                }

                require(bad == 0,
                        "full contents of all four arms agree with each other and the reference");
            }

            // The keys the segment just inserted, spot-checked on every arm: the scan above
            // proves the whole state, this proves the newest records are individually
            // addressable, which a scan-order bug could mask.
            for (int a = 0; a < 4; ++a) {
                for (std::size_t i = 0; i < seg.size();
                     i += std::max<std::size_t>(1, seg.size() / 16)) {
                    const Result r = idx[a]->find(seg[i].fresh, qmode);
                    require(r.found && r.value == seg[i].value, "spot check after replacement");
                }
            }

            if (frac == 2.00) {
                reclaims_seen = validation::e25::counts().subblock_reclaims;
                widened_seen = validation::e25::counts().reclaim_widened;
            }

            std::cout << "{\"kind\":\"" << kind << "\",\"backend\":\"" << backend_name
                      << "\",\"mode\":\"" << mode << "\",\"frac\":" << frac
                      << ",\"live\":" << ref.size()
                      << ",\"subblock_reclaims\":" << reclaims_seen
                      << ",\"reclaim_widened\":" << widened_seen << "}" << std::endl;

            std::cerr << kind << " " << backend_name << " " << mode << " frac=" << frac
                      << " ok" << std::endl;
            done = target;
        }

        // ---------------------------------------- delete-reinsert and duplicate insert
        // Exercise the two write paths a scan cannot see: removing a record and putting it
        // back, and offering a key that is already present (which must be refused and must
        // NOT overwrite the value).
        {
            const std::size_t kProbe = std::min<std::size_t>(512, live.size());

            for (int a = 0; a < 4; ++a) {
                std::size_t bad = 0;

                for (std::size_t i = 0; i < kProbe; ++i) {
                    const std::string k = live[i];
                    const std::uint64_t v = ref.at(k);

                    if (!idx[a]->erase(k)) ++bad;

                    if (idx[a]->find(k, qmode).found) ++bad;

                    if (!idx[a]->insert(k, v)) ++bad;

                    const Result r = idx[a]->find(k, qmode);

                    if (!r.found || r.value != v) ++bad;

                    if (idx[a]->insert(k, v + 0x9999)) ++bad;

                    if (idx[a]->find(k, qmode).value != v) ++bad;
                }

                require(bad == 0,
                        std::string("delete-reinsert / duplicate-insert on arm ") + ARM_NAMES[a]);
            }
        }

        // ------------------------------------------------- the lazy directory rebuild
        // The digests are derived state. This block observes the three moments that make the
        // invalidation path real instead of argued: empty before the first lookup, complete
        // after it, STALE after a split, and complete again after the next lookup.
        {
            Config small = cfg[2];
            small.region_keys = 8;

            auto s = std::make_unique<Index>(small);
            const std::size_t m = std::min<std::size_t>(keys.size(), 4096);
            std::vector<Pair> seed(keys.begin(), keys.begin() + m);
            s->bulk_load(seed);

            require(s->region_count() > 1, "the rebuild probe index has several regions");
            require(s->routing_regions() == 0,
                    "the digest directory starts empty: nothing has looked up yet");

            const Result warm = s->find(seed[0].first, qmode);
            require(warm.found && warm.value == seed[0].second,
                    "the warm-up lookup answers correctly");
            require(s->routing_regions() == s->region_count(),
                    "the first lookup built the digest directory for every region");

            // Force exactly one split, one insert at a time, and stop at the insert that
            // performed it. Nothing has looked up since, so the digests describe the
            // pre-split directory -- by construction, and the next assertion observes it.
            std::unordered_set<std::string> present;

            for (const auto& kv : seed) present.insert(kv.first);

            const std::size_t regions_before = s->region_count();
            bool split_seen = false;

            // 20,000, not 4,096: with 4,096 seed keys and region_keys = 8 the seed set
            // itself fills every region to exactly its target size, and one insertion per
            // seed key brings each region to exactly 2 * region_keys -- which is NOT past
            // the split threshold (> 2 * region_keys). The first version of this loop did
            // that and forced no split. Twenty thousand insertions put roughly five fresh
            // keys next to every seed key, so every region passes the threshold.
            for (int i = 0; i < 20000 && !split_seen; ++i) {
                std::string fresh =
                    seed[std::size_t(i) % seed.size()].first + "~g" + std::to_string(i);

                if (!present.insert(fresh).second) continue;

                s->insert(fresh, 0x9000ULL + std::size_t(i));

                if (s->region_count() > regions_before) split_seen = true;
            }

            require(split_seen, "a split was forced by insertions");

            if (split_seen) {
                require(s->routing_regions() == regions_before,
                        "the split left the digest directory STALE (the write path does not "
                        "rebuild it)");

                const Result after = s->find(seed[0].first, qmode);
                require(after.found, "the index still answers after the split");
                require(s->routing_regions() == s->region_count(),
                        "the next lookup rebuilt the digest directory after the split");
            }
        }

        // ------------------------------------------------------------------ report
        const std::uint64_t reclaims = validation::e25::counts().subblock_reclaims;
        const std::uint64_t widened = validation::e25::counts().reclaim_widened;

        std::cout << "{\"kind\":\"" << kind << "\",\"backend\":\"" << backend_name
                  << "\",\"mode\":\"" << mode << "\",\"keys\":" << N
                  << ",\"checks\":" << checks << ",\"failures\":" << failures
                  << ",\"subblock_reclaims\":" << reclaims
                  << ",\"reclaim_widened\":" << widened << "}" << std::endl;

        if (failures != 0) {
            std::cout << failures << " FAILED out of " << checks << "\n";
            return 1;
        }

        if (reclaims == 0 || widened == 0) {
            std::cout << "NOT EXERCISED: the widened reclaim branch never ran on this load "
                         "(subblock_reclaims="
                      << reclaims << ", reclaim_widened=" << widened
                      << "); the timing round would be certifying nothing about WIDE on " << kind
                      << "/" << backend_name << "\n";
            return 3;
        }

        std::cout << checks << "/" << checks << " checks passed; subblock_reclaims=" << reclaims
                  << " reclaim_widened=" << widened << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
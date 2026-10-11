// G5 external baseline: a mature B+ tree measured in the same process, on the same workload,
// with the same three semantics the task book asked for -- string keys, value ownership, and
// range scan.
//
// The literature task book (§十) requires a B+ tree candidate that supports the SAME string
// keys, value ownership and scan semantics as the prototype. libart is already the standing
// external baseline, but it does not cover the ordered-container class: it is a radix tree and
// its scan path in this project is an adapter, not the library's own iterator. So this program
// adds a real B+ tree.
//
// WHICH B+ TREE, AND WHY NOT THE OBVIOUS ONE. The classic STX B+ tree (bingmann/stx-btree) is
// GPL-3.0; vendoring it would relicense this project, and that decision is the project owner's,
// not this benchmark's. Its maintained successor TLX (tlx/tlx) is BSL-1.0 (permissive) and
// carries the same B+ tree as tlx::btree_map. third_party/fetch_tlx_btree.py pins the commit and
// verifies each header by SHA-256 and byte length, the same way fetch_libart.py does.
//
//   results/main/long_btree_<mode>.jsonl
//
// FOUR arms:
//   S0     the shipped configuration                        <- the baseline
//   WIDE   S0 + subblock_reclaim + reclaim_widest           <- G3's A1', switched on here
//   ART    libart, the standing external baseline
//   BTREE  tlx::btree_map<std::string, std::uint64_t>, the new external baseline
//
// SEMANTIC PARITY, stated explicitly because a baseline that does not do the same job is not a
// baseline:
//   * keys    -- std::string, the full key, no truncation, no prefix compression
//   * values  -- std::uint64_t held BY VALUE in the container, exactly as Index and libart
//   * scan    -- from the first key >= start, at most n records, full key and value, ascending
//   * updates -- erase(victim) then insert(fresh) of a different key, live count held constant
// The scan is checked against the reference map for EVERY scanning arm at every checkpoint: a
// timing taken on a scan that returns the wrong records would otherwise be invisible.
//
// WHAT THE STRUCTURE COLUMNS MEAN. `overflow` and `memory` are the prototype's own accounting;
// libart exposes neither, and TLX's counters are node counts rather than bytes, so the external
// arms do not carry comparable byte figures. They are written as 0 with the report stating that
// 0 means "not applicable", never "zero". The byte comparison against the external baselines is
// made separately, by peak RSS of a process that builds only that container.
//
// The arm order is ROTATED per repetition, so no arm is systematically first or last.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "native_e22.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <tlx/container/btree_map.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

using namespace validation;
using namespace validation::e9alloc;

using Clock = std::chrono::steady_clock;
using Reference = std::map<std::string, std::uint64_t, Less>;
using BTreeMap = tlx::btree_map<std::string, std::uint64_t>;

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// FNV-1a over the key bytes and the value. Used to compare a scan's OUTPUT, not just its cost.
static std::uint64_t scan_hash(std::uint64_t h, const std::string& k, std::uint64_t v) {
    for (unsigned char c : k) {
        h ^= c;
        h *= 1099511628211ULL;
    }

    h ^= v;
    h *= 1099511628211ULL;

    return h;
}

struct Pipeline {
    std::vector<Alloc> s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};

static Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
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

static auto sg_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint16_t>> o;

    for (const auto& a : v) o.push_back(a.sg);

    return o;
}

static auto bb_of(const std::vector<Alloc>& v) {
    std::vector<std::vector<std::uint32_t>> o;

    for (const auto& a : v) o.push_back(a.bb);

    return o;
}

int main(int argc, char** argv) {
    try {
#ifdef __linux__
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 4)
            throw std::invalid_argument("bench_btree KIND N REPS [TRANCOCSV] [RUNTAG] [MODE]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const int reps = std::stoi(argv[3]);
        const std::string trcsv = argc > 4 ? argv[4] : "";
        const std::string run_tag = argc > 5 ? argv[5] : "";
        const std::string mode = argc > 6 ? argv[6] : "grow";

        if (mode != "grow" && mode != "same")
            throw std::invalid_argument("MODE must be grow or same");

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const std::string frozen_rel =
            "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
        std::uint32_t thr = 32;
        {
            std::ifstream fz(frozen_rel);
            if (!fz) throw std::runtime_error("frozen file missing");
            std::stringstream ss; ss << fz.rdbuf();
            const std::string t = ss.str();
            const auto p = t.find("\"frozen_hybrid_threshold\":");
            if (p == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(t.substr(p + 26)));
        }

        Config base = e25sw::baseline_config(thr);
        base.write_fast_locate = true;
        base.reuse_locate_ctx = true;
        base.scan_reserve_result = true;
        base.compact_reuse_tag = true;
        base.compact_needs_garbage = true;

        // Two Config arms plus two external containers. The single variable between S0 and WIDE
        // is asserted, so an arm that silently became a copy of another fails here rather than
        // passing a comparison that never happened. ART and BTREE are not Configs, so they are
        // not part of that assertion.
        Config cfg[2] = {base, base};
        cfg[1].subblock_reclaim = true;
        cfg[1].reclaim_widest = true;
        e25sw::require_only_switches_differ(cfg[0], cfg[1], {"subblock_reclaim", "reclaim_widest"});

        const char* names[4] = {"S0", "WIDE", "ART", "BTREE"};
        const int ARMS = 4;
        const int ART_ARM = 2;
        const int BTREE_ARM = 3;

        Pipeline plan;
        {
            Index probe(base);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        // A fixed query sequence, timed at each checkpoint.
        std::vector<std::string> qs;
        {
            std::mt19937_64 rng(2026092003);
            const std::size_t n = std::min<std::size_t>(16384, N);
            for (std::size_t i = 0; i < n; ++i) qs.push_back(keys[rng() % N].first);
        }

        // A fixed scan sequence. The starts are drawn from the SAME generator family as the
        // query keys but with their own seed, and every scanning arm is checked against the
        // reference at every checkpoint, so a scan that returns the wrong records cannot pass.
        const std::size_t SCAN_STARTS = 64;
        const std::size_t SCAN_N = 32;
        std::vector<std::string> sc;
        {
            std::mt19937_64 rng(2026092301);
            for (std::size_t i = 0; i < SCAN_STARTS; ++i) sc.push_back(keys[rng() % N].first);
            // Two boundary starts that are deliberately NOT present as keys, so "start falls
            // between two keys" is exercised rather than assumed.
            sc.push_back(keys.front().first.substr(0, keys.front().first.size() / 2) + "zzzz");
            sc.push_back(std::string());
        }

        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};

        std::ofstream out(("results/main/long_btree_" + mode + ".jsonl").c_str(), std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (int r = 0; r < reps; ++r) {
            int order[4] = {r % ARMS, (r + 1) % ARMS, (r + 2) % ARMS, (r + 3) % ARMS};

            for (int oi = 0; oi < ARMS; ++oi) {
                const int arm = order[oi];
                const bool is_art = (arm == ART_ARM);
                const bool is_btree = (arm == BTREE_ARM);

                std::deque<std::string> live;
                std::unordered_set<std::string> live_set;
                Reference ref;
                for (const auto& kv : keys) {
                    live.push_back(kv.first);
                    ref[kv.first] = kv.second;
                    live_set.insert(kv.first);
                }

                std::unique_ptr<Index> s;
                std::unique_ptr<e22art::NativeArt> art;
                std::unique_ptr<BTreeMap> bt;

                if (is_art) {
                    art = std::make_unique<e22art::NativeArt>(keys);
                } else if (is_btree) {
                    bt = std::make_unique<BTreeMap>();
                    for (const auto& kv : keys) bt->insert(std::make_pair(kv.first, kv.second));
                } else {
                    s = std::make_unique<Index>(cfg[arm]);
#ifdef E25_DIAG
                    validation::e25::counts().reset();
#endif
                    s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
                }

                const std::size_t live0 = is_art ? art->size() : (is_btree ? bt->size() : s->size());
                std::size_t done = 0;
                double cum = 0;
                std::uint64_t serial = 0;

                for (double frac : CPS) {
                    const std::size_t target = static_cast<std::size_t>(double(N) * frac);
                    if (target <= done) continue;

                    struct Move { std::string victim, fresh; std::uint64_t value; };
                    std::vector<Move> seg;
                    seg.reserve(target - done);

                    for (std::size_t i = done; i < target; ++i) {
                        const std::string victim = live.front();
                        live.pop_front();
                        live_set.erase(victim);

                        std::string basek = victim;
                        const auto pos = basek.find("~r");
                        if (pos != std::string::npos) basek.resize(pos);

                        std::string fresh;
                        if (mode == "grow") {
                            fresh = basek + "~r" + std::to_string(serial);
                        } else {
                            fresh = basek;
                            bool ok = false;
                            for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                                fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                                if (fresh != victim && ref.find(fresh) == ref.end() &&
                                    live_set.find(fresh) == live_set.end()) { ok = true; break; }
                            }
                            if (!ok) throw std::runtime_error("same-length replacement exhausted its alphabet");
                        }

                        const std::uint64_t val = 0x7000ULL + serial;
                        ++serial;
                        seg.push_back({victim, fresh, val});
                        live.push_back(fresh);
                        live_set.insert(fresh);
                    }

                    for (const auto& m : seg) {
                        if (ref.erase(m.victim) != 1) throw std::runtime_error("erase target absent");
                        if (ref.find(m.fresh) != ref.end()) throw std::runtime_error("insert target present");
                        ref[m.fresh] = m.value;
                    }

                    const auto a = Clock::now();

                    for (const auto& m : seg) {
                        if (is_art) {
                            art->erase(m.victim);
                            art->upsert(m.fresh, m.value);
                        } else if (is_btree) {
                            bt->erase(m.victim);
                            bt->insert(std::make_pair(m.fresh, m.value));
                        } else {
                            s->erase(m.victim);
                            s->insert(m.fresh, m.value);
                        }
                    }

                    cum += ms_between(a, Clock::now());
                    done = target;

                    const std::size_t now_live = is_art ? art->size() : (is_btree ? bt->size() : s->size());
                    if (now_live != ref.size())
                        throw std::runtime_error("live count drifted from the reference");

                    // ---- point queries ----
                    std::uint64_t q_acc = 0;
                    const auto q0 = Clock::now();

                    for (const auto& k : qs) {
                        if (is_art) {
                            const Result rr = art->find(k);
                            q_acc += rr.value + rr.found;
                        } else if (is_btree) {
                            const auto it = bt->find(k);
                            q_acc += it->second + (it != bt->end());
                        } else {
                            const Result rr = s->find(k, qmode);
                            q_acc += rr.value + rr.found;
                        }
                    }

                    const double q_ms = ms_between(q0, Clock::now());

                    // ---- range scans ----
                    // ART is deliberately NOT timed here. E23 measured this adapter's scan at
                    // 40x-3823x the prototype and concluded that figure is the ADAPTER's cost,
                    // not libart's: libart exposes no seek-to-key iterator, so the adapter
                    // walks the whole tree. Repeating a known apparatus artefact would not be a
                    // baseline. ART's scan columns are 0 and the report says so.
                    std::uint64_t sc_acc = 1469598103934665603ULL;
                    std::uint64_t sc_exp = 1469598103934665603ULL;
                    double sc_ms = 0;

                    if (!is_art) {
                        // Compared PER START, not as one accumulated hash: "the scans differ"
                        // is not a diagnosis. The first mismatching start is named, with the
                        // record counts on both sides. Both accumulators are folded into the
                        // record as well, so a reader can re-check the equality offline.
                        //
                        // THE REFERENCE WALK IS OUTSIDE THE TIMED SPAN. Computing it inside
                        // would add the same reference-map work to every arm's scan_ms, which
                        // does not cancel: it attenuates all the paired ratios toward 1 and
                        // makes the baseline look closer to the prototype than it is. The
                        // update loop above already keeps its reference upkeep outside the
                        // timed span for the same reason.
                        sc_exp = 1469598103934665603ULL;
                        std::vector<std::uint64_t> want(sc.size());
                        std::vector<std::size_t> want_n(sc.size());

                        for (std::size_t si = 0; si < sc.size(); ++si) {
                            std::uint64_t h = 1469598103934665603ULL;
                            std::size_t n = 0;

                            for (auto it = ref.lower_bound(sc[si]); it != ref.end() && n < SCAN_N; ++it, ++n) {
                                h = scan_hash(h, it->first, it->second);
                            }

                            want[si] = h;
                            want_n[si] = n;
                            sc_exp = scan_hash(sc_exp, sc[si], h);
                        }

                        const auto s0 = Clock::now();

                        for (std::size_t si = 0; si < sc.size(); ++si) {
                            const std::string& k = sc[si];
                            std::uint64_t mine = 1469598103934665603ULL;
                            std::size_t got = 0;

                            if (is_btree) {
                                for (auto it = bt->lower_bound(k); it != bt->end() && got < SCAN_N; ++it, ++got) {
                                    mine = scan_hash(mine, it->first, it->second);
                                }
                            } else {
                                const std::vector<Pair> rows = s->scan(k, SCAN_N);
                                got = rows.size();

                                for (const auto& p : rows) mine = scan_hash(mine, p.first, p.second);
                            }

                            sc_acc = scan_hash(sc_acc, k, mine);

                            if (mine != want[si]) {
                                std::ostringstream msg;
                                msg << "scan output differs from the reference: arm " << names[arm]
                                    << " start#" << si << " key=\"" << k << "\" got " << got
                                    << " record(s), reference has " << want_n[si];
                                throw std::runtime_error(msg.str());
                            }
                        }

                        sc_ms = ms_between(s0, Clock::now());
                    }

                    // Spot-check that the container really holds what the reference says, so a
                    // timing on a silently-nonfunctional workload is impossible.
                    for (std::size_t i = 0; i < seg.size(); i += std::max<std::size_t>(1, seg.size() / 16)) {
                        const std::string& k = seg[i].fresh;
                        const std::uint64_t want = seg[i].value;
                        bool found = false;
                        std::uint64_t got = 0;

                        if (is_art) {
                            const Result rr = art->find(k);
                            found = rr.found;
                            got = rr.value;
                        } else if (is_btree) {
                            const auto it = bt->find(k);
                            found = (it != bt->end());
                            got = found ? it->second : 0;
                        } else {
                            const Result rr = s->find(k, Mode{Route::Interval, Prefetch::Off, true, true,
                                                              OvSearch::Hybrid, thr, false});
                            found = rr.found;
                            got = rr.value;
                        }

                        if (!found || got != want)
                            throw std::runtime_error("spot check failed after replacement");
                    }

                    // The external arms carry no comparable structural columns; see the header.
                    const std::size_t st_overflow = (is_art || is_btree) ? 0 : s->overflow_count();
                    const std::size_t st_compactions = (is_art || is_btree) ? 0 : s->compaction_count();
                    const std::size_t st_memory = (is_art || is_btree) ? 0 : s->memory();
                    // TLX node counts ARE well defined, so they are recorded as themselves: a
                    // deterministic count, not a byte estimate dressed up as one.
                    const std::size_t bt_nodes = is_btree ? bt->get_stats().nodes() : 0;
                    const std::size_t bt_leaves = is_btree ? bt->get_stats().leaves : 0;

#ifdef E25_DIAG
                    const auto& C = validation::e25::counts();
                    const std::uint64_t replayed = C.compact_records_replayed;
                    const std::uint64_t copied = C.compact_bytes_copied;
                    const std::uint64_t garbage = C.compact_garbage_before;
                    const std::uint64_t helped = C.compact_helped;
#else
                    const std::uint64_t replayed = 0, copied = 0, garbage = 0, helped = 0;
#endif

                    out << "{\"kind\":\"" << kind << "\",\"rep\":" << r
                        << ",\"arm\":\"" << names[arm] << "\",\"run_tag\":\"" << run_tag << "\""
                        << ",\"mode\":\"" << mode << "\",\"frac\":" << frac
                        << ",\"cycles\":" << done
                        << ",\"live\":" << now_live
                        << ",\"live0\":" << live0
                        << ",\"cum_ms\":" << cum
                        << ",\"overflow\":" << st_overflow
                        << ",\"compactions\":" << st_compactions
                        << ",\"memory\":" << st_memory
                        << ",\"replayed\":" << replayed
                        << ",\"copied\":" << copied
                        << ",\"garbage\":" << garbage
                        << ",\"helped\":" << helped
                        << ",\"query_ms\":" << q_ms
                        << ",\"query_acc\":" << q_acc
                        << ",\"scan_ms\":" << sc_ms
                        << ",\"scan_acc\":" << sc_acc
                        << ",\"scan_exp\":" << sc_exp
                        << ",\"btree_nodes\":" << bt_nodes
                        << ",\"btree_leaves\":" << bt_leaves
                        << "}" << std::endl;
                }

                std::cerr << kind << " rep" << r << " " << names[arm] << " done" << std::endl;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

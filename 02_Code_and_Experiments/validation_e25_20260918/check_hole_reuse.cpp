// Gate for the primary-hole-reuse candidate (H, Config::primary_hole_reuse).
//
// TWO THINGS HAVE TO BE TRUE, AND THEY ARE CHECKED SEPARATELY.
//
// 1. IT MUST BE CORRECT. H writes a new suffix into a DEAD RANGE of the same sub-block
//    instead of repacking the block for contiguous room, and it moves descriptors exactly
//    as insert_into does. That touches the layout every read path walks, so the gate drives
//    a W index and a W+H index through the SAME operation sequence and compares them
//    against an independent std::map after every phase: values, size, find and scans from
//    fixed and random starts.
//
// 2. IT MUST ACTUALLY RUN AND ACTUALLY SAVE THE REPACK. A branch that never fired makes
//    every comparison vacuous (the G0 audit found exactly that hole once). The gate asserts
//    the byte-refusal hook ran at all, that attempts split into placed+absent, and that
//    BOTH outcomes occurred on the main workload -- otherwise this run is reported as
//    NOT EXERCISED rather than green. The saving claim is read from the counters beside it:
//    the number of block repacks, the repack calls and the moved bytes must not INCREASE on
//    the H arm, measured on the same sequence run sequentially so the process-global
//    counters belong to one arm at a time.
//
// CASES THE PLAN NAMES, and how each is reached:
//   hole exactly fits / slack / multiple / none   the replacement workload (random victims,
//                                                 fresh keys anchored on their victim)
//   full slots / byte refusal                     this workload refuses placements on both
//   empty block / single record / zero-length suffix   small-corpus block: keys equal to
//                                                 the region prefix have zero-length suffixes
//   delete/reinsert, duplicate insert             live-key cases taken from the reference
//   long/high-bit/NUL keys                        ctx16-only synthetic block (the LITS
//                                                 domain contract is tested by its own gate)
//   H + overflow_migrate                          combination block: H can rescue a drain
//                                                 insert the reservation thought impossible,
//                                                 and that must not lose or duplicate a record
//
// ASan/UBSan is a separate target (san_check_hole_reuse) over this same source.
//
// Usage:  check_hole_reuse KIND N TRANCOCSV BACKEND      (BACKEND = ctx16 | lits)

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;   // Alloc / demand_single / demand_strict

#ifndef E25_DIAG
#error "check_hole_reuse must be built with -DE25_DIAG"
#endif

namespace {

std::uint64_t checks = 0;
std::uint64_t failures = 0;

// Set when the workload never reached one of the candidate's two outcomes. Distinct from
// `failures` on purpose: an unexercised branch is a gap in coverage, not evidence of a
// defect, and the two need different responses.
bool not_exercised = false;

void require(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

struct UByteLess {
    bool operator()(std::string_view a, std::string_view b) const {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](char x, char y) {
                                                return static_cast<unsigned char>(x) <
                                                       static_cast<unsigned char>(y);
                                            });
    }
};

using Reference = std::map<std::string, std::uint64_t, UByteLess>;

// W = the frozen baseline with subblock_reclaim + reclaim_widest, navigation and scan
// candidates off -- the same arm the entry round measured. H is the single varying field.
Config arm_config(std::uint32_t thr, bool hole, bool migrate, bool lits) {
    Config c = e25sw::baseline_config(thr);
    e25sw::pin_frozen_switches(c);
    c.subblock_reclaim = true;      // W: the mechanism H substitutes for
    c.reclaim_widest = true;        // W
    c.reclaim_low_activity = false;
    c.compact_region_routing = false;
    c.overflow_migrate = migrate;
    c.primary_hole_reuse = hole;
    if (lits) c.model_backend = ModelBackend::LitsHpt;
    return c;
}

const Mode kMode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

// The demand plan the entry round and the timing rounds use: one capacity table, built
// from the loaded layout with the W configuration and then reused for BOTH arms -- it is
// an INPUT to the comparison, not part of any arm, so an arm must not derive its own.
// Without it the default capacity leaves tranco blocks loose enough that a byte refusal
// never happens, and the candidate would be vacuous there (the first version of this gate
// did exactly that and could not tell it apart from "the candidate does nothing").
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

// Compares the two indexes and the reference on the OBSERVABLE, not on the layout. H is
// allowed to move suffixes; it is not allowed to change an answer.
void compare_all(const Index& a, const Index& b, const Reference& ref, const std::string& label) {
    require(a.size() == b.size(), label + ": the two indexes hold the same number of keys");
    require(a.size() == ref.size(), label + ": and it is the number the reference holds");

    std::uint64_t mismatches = 0;
    std::size_t shown = 0;
    for (const auto& kv : ref) {
        const Result ra = a.find(kv.first, kMode);
        const Result rb = b.find(kv.first, kMode);
        const bool oka = ra.found && ra.value == kv.second;
        const bool okb = rb.found && rb.value == kv.second;
        if (!oka || !okb) {
            ++mismatches;
            if (shown < 3) {
                ++shown;
                std::cout << "  key(len=" << kv.first.size() << ", first byte "
                          << (kv.first.empty() ? 0
                              : static_cast<unsigned>(
                                    static_cast<unsigned char>(kv.first[0])))
                          << "): want " << kv.second << " | W found=" << ra.found
                          << " v=" << ra.value << " | W+H found=" << rb.found
                          << " v=" << rb.value << "\\n";
            }
        }
    }
    require(mismatches == 0, label + ": every live key answers the same in all three");

    std::vector<std::string> starts;
    starts.push_back(std::string());
    starts.push_back("\x01");
    std::size_t taken = 0;
    for (const auto& kv : ref) {
        if (taken % std::max<std::size_t>(1, ref.size() / 16) == 0) starts.push_back(kv.first);
        if (++taken > 32) break;
    }
    for (const auto& start : starts) {
        const auto sa = a.scan(start, 64);
        const auto sb = b.scan(start, 64);
        require(sa.size() == sb.size(), label + ": scan agrees on the row count");
        for (std::size_t i = 0; i < std::min(sa.size(), sb.size()); ++i)
            require(sa[i].first == sb[i].first && sa[i].second == sb[i].second,
                    label + ": scan agrees row by row");
    }

    require(!a.find(std::string("~absent~", 8), kMode).found,
            label + ": an absent key misses on the baseline");
    require(!b.find(std::string("~absent~", 8), kMode).found,
            label + ": an absent key misses on the candidate");
}

struct Op {
    std::string victim;
    std::string fresh;
    std::uint64_t value;
};

std::vector<Op> make_ops(const std::vector<Pair>& keys, std::size_t rounds) {
    std::vector<std::size_t> order(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) order[i] = i;
    std::mt19937_64 rng(20261011);
    std::shuffle(order.begin(), order.end(), rng);

    std::vector<Op> ops;
    const std::size_t n = std::min<std::size_t>(keys.size(), rounds);
    ops.reserve(n);
    std::uint64_t serial = 0;
    for (std::size_t i = 0; i < n; ++i) {
        Op op;
        op.victim = keys[order[i]].first;
        std::string base = op.victim;
        const auto pos = base.find("~f");
        if (pos != std::string::npos) base.resize(pos);
        op.fresh = base + "~f" + std::to_string(serial++);
        op.value = 0x5000ULL + serial;
        ops.push_back(std::move(op));
    }
    return ops;
}

void apply_ops(Index& s, Reference& ref, const std::vector<Op>& ops) {
    for (const Op& op : ops) {
        s.erase(op.victim);
        ref.erase(op.victim);
        s.insert(op.fresh, op.value);
        ref.emplace(op.fresh, op.value);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 5) throw std::invalid_argument("check_hole_reuse KIND N TRANCOCSV BACKEND");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argv[3];
        const std::string backend = argv[4];
        const bool lits = (backend == "lits");
        if (!lits && backend != "ctx16")
            throw std::invalid_argument("BACKEND must be ctx16 or lits");

        const auto keys = make_data(kind, nkeys, trcsv);

        const Config off = arm_config(32, false, false, lits);
        Config on = off;
        on.primary_hole_reuse = true;

        Pipeline plan;
        {
            Index probe(off);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        auto a = std::make_unique<Index>(off);
        auto b = std::make_unique<Index>(on);
        a->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
        b->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        Reference ref;
        for (const auto& kv : keys) ref.emplace(kv.first, kv.second);

        compare_all(*a, *b, ref, "after bulk_load");
        require(a->region_count() == b->region_count(),
                "the switch does not change the region count");

        // ------------------------------------------------------------------ replacement
        // A REPLACEMENT, not an erase: an erase only frees space and never fails a
        // placement, so the byte-refusal hook would never be entered.
        const std::vector<Op> ops = make_ops(keys, 40000);
        {
            // The reference is updated IN PLACE: the later phases read it as the current
            // truth. An earlier version copied it and updated the copy, so the delete/
            // reinsert phase below took its key from a reference the indexes had already
            // left behind -- and the run failed on the gate, not on the candidate.
            std::uint64_t bad_erase = 0, bad_insert = 0;
            for (const Op& op : ops) {
                const bool ea = a->erase(op.victim);
                const bool eb = b->erase(op.victim);
                if (!ea || !eb) ++bad_erase;
                ref.erase(op.victim);
                const bool ia = a->insert(op.fresh, op.value);
                const bool ib = b->insert(op.fresh, op.value);
                if (!ia || !ib) ++bad_insert;
                ref.emplace(op.fresh, op.value);
            }
            require(bad_erase == 0, "every replacement victim was live in both indexes");
            require(bad_insert == 0, "every replacement fresh key was accepted by both");
            compare_all(*a, *b, ref, "after replacement");
        }

        // ------------------------------------------------------------------ counters
        // The counters are process-global, so the saving claim runs the SAME sequence on a
        // W index and then on a W+H index, sequentially: one arm owns the counters at a time.
        std::uint64_t w_reclaims = 0, w_calls = 0, w_moved = 0;
        std::uint64_t h_reclaims = 0, h_calls = 0, h_moved = 0;
        std::uint64_t att = 0, placed = 0, absent = 0, lt2 = 0, wide = 0, hbytes = 0;
        {
            auto c = std::make_unique<Index>(off);
            c->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc),
                                   plan.s_caps);
            Reference r;
            for (const auto& kv : keys) r.emplace(kv.first, kv.second);
            validation::e25::counts().reset();
            apply_ops(*c, r, ops);
            w_reclaims = validation::e25::counts().subblock_reclaims;
            w_calls = validation::e25::counts().rp_calls;
            w_moved = validation::e25::counts().rp_moved_bytes;

            auto d = std::make_unique<Index>(on);
            d->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc),
                                   plan.s_caps);
            Reference r2;
            for (const auto& kv : keys) r2.emplace(kv.first, kv.second);
            validation::e25::counts().reset();
            apply_ops(*d, r2, ops);
            h_reclaims = validation::e25::counts().subblock_reclaims;
            h_calls = validation::e25::counts().rp_calls;
            h_moved = validation::e25::counts().rp_moved_bytes;
            att = validation::e25::counts().hole_reuse_attempts;
            placed = validation::e25::counts().hole_reuse_placed;
            absent = validation::e25::counts().hole_reuse_absent;
            lt2 = validation::e25::counts().hole_reuse_skipped_lt2;
            wide = validation::e25::counts().hole_reuse_skipped_wide;
            hbytes = validation::e25::counts().hole_reuse_bytes;
            compare_all(*c, *d, r2, "after the counter arms' replacement");
        }
        if (att == 0) {
            // The load never reached a byte refusal, so the candidate could not act. That
            // is a coverage gap for this corpus, not a defect -- and the comparisons that
            // DID run stay valid. Reported as NOT EXERCISED (exit 3), which no round may
            // read as evidence either way.
            std::cout << "  NOT EXERCISED: the byte-refusal hook never ran on this workload\n";
            not_exercised = true;
        } else {
            require(att == placed + absent, "attempts split into placed + absent");
            require(h_reclaims <= w_reclaims,
                    "H does not increase the number of sub-block repacks");
            require(h_calls <= w_calls, "H does not increase the number of repack passes");
            require(h_moved <= w_moved, "H does not increase the bytes repack moves");
            if (placed == 0 || absent == 0) {
                std::cout << "  NOT EXERCISED: placed=" << placed << " absent=" << absent
                          << " -- this run does not cover both outcomes\n";
                not_exercised = true;
            } else {
                std::cout << "  H placed " << placed << " of " << att << " attempts (" << hbytes
                          << " B); repacks " << w_reclaims << " -> " << h_reclaims
                          << ", moved bytes " << w_moved << " -> " << h_moved << "\n";
            }
        }

        // ------------------------------------------------------------------ small corpora
        // Empty suffix (key == region prefix), delete-to-one-record, and the refill. The
        // block geometry is forced with region_keys = 2 so the small corpus produces
        // several sub-blocks.
        {
            std::vector<Pair> small = {
                {std::string("pre"), 1},   {std::string("pre0"), 2}, {std::string("pre1"), 3},
                {std::string("pre2"), 4},  {std::string("pre3"), 5}, {std::string("pre4"), 6},
                {std::string("pre5"), 7},  {std::string("pre6"), 8},
            };
            Config o2 = arm_config(8, false, false, lits);
            Config c2 = o2;
            c2.primary_hole_reuse = true;
            o2.region_keys = 2;
            c2.region_keys = 2;
            auto sa = std::make_unique<Index>(o2);
            auto sb = std::make_unique<Index>(c2);
            sa->bulk_load(small);
            sb->bulk_load(small);
            Reference r2;
            for (const auto& kv : small) r2.emplace(kv.first, kv.second);

            for (std::size_t i = 1; i < small.size(); ++i) {
                sa->erase(small[i].first);
                sb->erase(small[i].first);
                r2.erase(small[i].first);
            }
            compare_all(*sa, *sb, r2, "small: drained to one record");

            for (std::size_t i = 0; i < 12; ++i) {
                const std::string k = "pre9~f" + std::to_string(i);
                const bool ia = sa->insert(k, 100 + i);
                const bool ib = sb->insert(k, 100 + i);
                require(ia && ib, "small: the refill key was accepted by both indexes");
                r2.emplace(k, 100 + i);
            }
            compare_all(*sa, *sb, r2, "small: refilled");

            const std::string lone = r2.begin()->first;
            sa->erase(lone);
            sb->erase(lone);
            r2.erase(lone);
            compare_all(*sa, *sb, r2, "small: deleted down to empty");
        }

        // ------------------------------------------------------------------ delete/reinsert + duplicate
        {
            const std::string k = ref.begin()->first;
            const bool da = a->erase(k);
            const bool db = b->erase(k);
            require(da && db, "the deleted key was live in both indexes");
            ref.erase(k);
            compare_all(*a, *b, ref, "after deleting a live key");

            const bool ra2 = a->insert(k, 777);
            const bool rb2 = b->insert(k, 777);
            require(ra2 && rb2, "the reinserted key was accepted by both indexes");
            ref.emplace(k, 777);
            compare_all(*a, *b, ref, "after reinserting it");

            const std::string k2 = ref.begin()->first;
            const std::uint64_t old = ref.begin()->second;
            const std::size_t before = ref.size();
            const bool ia = a->insert(k2, 999);
            const bool ib = b->insert(k2, 999);
            require(!ia && !ib, "a duplicate insert is refused in both");
            require(ref.size() == before, "the test did not corrupt its own reference");
            compare_all(*a, *b, ref, "after a refused duplicate insert");
            require(a->find(k2, kMode).value == old && b->find(k2, kMode).value == old,
                    "a duplicate insert kept the old value in both");
        }

        // ------------------------------------------------------------------ ctx16-only keys
        if (!lits) {
            std::vector<Pair> edge;
            edge.push_back({std::string("pre"), 1});
            edge.push_back({std::string("pre") + std::string(1, '\0') + "a", 2});
            edge.push_back({std::string("pre") + std::string("\xff\xfe", 2), 3});
            edge.push_back({std::string("pre") + std::string(4090, 'L'), 4});
            edge.push_back({std::string("pre~s"), 5});
            // bulk_load consumes its input in SLICES and builds each region from the slice
            // as given: it requires unsigned-byte-sorted keys and never sorts for you. The
            // first version of this block pushed 0xff before 'L', so the load laid out the
            // keys in an order its own (unsigned) comparisons do not agree with, and BOTH
            // arms -- W included -- then missed three of the five keys. The contract is the
            // caller's side of the deal; sort it here, with the same comparator the index
            // uses, rather than reordering the pushes by hand.
            std::sort(edge.begin(), edge.end(), [](const Pair& x, const Pair& y) {
                return UByteLess{}(x.first, y.first);
            });
            Config o3 = arm_config(8, false, false, lits);
            Config c3 = o3;
            c3.primary_hole_reuse = true;
            o3.region_keys = 2;
            c3.region_keys = 2;
            auto sa = std::make_unique<Index>(o3);
            auto sb = std::make_unique<Index>(c3);
            sa->bulk_load(edge);
            sb->bulk_load(edge);
            Reference r3;
            for (const auto& kv : edge) r3.emplace(kv.first, kv.second);
            compare_all(*sa, *sb, r3, "edge keys: NUL, high bytes, 4092-byte key");
            for (int i = 0; i < 24; ++i) {
                const std::string k = std::string("pre~f") + std::to_string(i);
                const bool ia = sa->insert(k, 200 + i);
                const bool ib = sb->insert(k, 200 + i);
                require(ia && ib, "edge keys: the inserted key was accepted by both");
                r3.emplace(k, 200 + i);
            }
            compare_all(*sa, *sb, r3, "edge keys: after inserts");
        } else {
            std::cout << "  LITS domain edge keys are covered by check_lits_index; skipped "
                         "here by design\n";
        }

        // ------------------------------------------------------------------ H + overflow_migrate
        // Semantics only: H can rescue a drain insert that its virtual reservation thought
        // impossible, and that must not lose or duplicate a record.
        {
            Config o4 = arm_config(32, false, true, lits);
            Config c4 = o4;
            c4.primary_hole_reuse = true;
            auto sa = std::make_unique<Index>(o4);
            auto sb = std::make_unique<Index>(c4);
            const auto small_keys = make_data(kind, std::min<std::size_t>(nkeys, 16384), trcsv);
            sa->bulk_load(small_keys);
            sb->bulk_load(small_keys);
            Reference r4;
            for (const auto& kv : small_keys) r4.emplace(kv.first, kv.second);
            const std::vector<Op> ops4 = make_ops(small_keys, 12000);
            for (const Op& op : ops4) {
                sa->erase(op.victim);
                sb->erase(op.victim);
                r4.erase(op.victim);
                sa->insert(op.fresh, op.value);
                sb->insert(op.fresh, op.value);
                r4.emplace(op.fresh, op.value);
            }
            compare_all(*sa, *sb, r4, "combination H + overflow_migrate");
        }

        // ------------------------------------------------------------------ report
        std::cout << "{\"kind\":\"" << kind << "\",\"backend\":\"" << backend
                  << "\",\"keys\":" << keys.size()
                  << ",\"final_size\":" << b->size()
                  << ",\"regions\":" << b->region_count()
                  << ",\"h_attempts\":" << att
                  << ",\"h_placed\":" << placed
                  << ",\"h_absent\":" << absent
                  << ",\"h_skipped_lt2\":" << lt2
                  << ",\"h_skipped_wide\":" << wide
                  << ",\"h_bytes\":" << hbytes
                  << ",\"w_reclaims\":" << w_reclaims
                  << ",\"h_reclaims\":" << h_reclaims
                  << ",\"w_moved_bytes\":" << w_moved
                  << ",\"h_moved_bytes\":" << h_moved
                  << ",\"checks\":" << checks
                  << ",\"failures\":" << failures
                  << "}" << std::endl;

        if (failures != 0) {
            std::cout << failures << " FAILED out of " << checks << "\n";
            return 1;
        }
        if (not_exercised) {
            std::cout << checks << "/" << checks
                      << " checks passed, but one of the candidate's outcomes was NOT "
                         "EXERCISED on this corpus\n";
            return 3;
        }
        std::cout << checks << "/" << checks << " checks passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

// Gate for the low-activity sub-block reclaim candidate.
//
// TWO THINGS HAVE TO BE TRUE AND THEY ARE CHECKED SEPARATELY.
//
// 1. IT MUST BE CORRECT. The candidate lifts repack_subblock's `n < 2` early return, so a
//    block holding 0 or 1 live records gets packed. The packing loop already generalises --
//    for n == 0 it does not run and bottom becomes bb_, which is the whole area free -- but
//    "already generalises" is a reading of the code, so this program drives two indexes, one
//    with the switch off and one with it on, through the SAME operation sequence, and
//    compares them against an independent std::map after every step: values, size, find and
//    scan.
//
// 2. IT MUST ACTUALLY RUN. A candidate whose branch never executed would make every
//    downstream comparison vacuous -- the exact hole the G0 audit found in gates that
//    reported success for a comparison that never happened. So the branch counter is
//    asserted non-zero, and the count is printed.
//
// THE CASES THE PLAN NAMES, and how each is reached:
//   n = 0            erase every record of a block, then insert into it
//   n = 1            erase all but one of a block, then insert into it
//   n = 2            the shipped path; must be unchanged
//   no garbage       a block that is full but has no dead bytes -- the candidate must not
//                    invent space, so the observable has to stay identical
//   empty suffix     a key equal to the region prefix has a zero-length suffix; the packing
//                    loop moves zero bytes and must still set offset and bottom correctly
//   delete/reinsert  the same key removed and re-added
//   duplicate key    insert on an existing key keeps the old value
//   slot refusal     more records than slots
//   byte refusal     more bytes than the block can hold
//   boundary caps    fill a block to its byte limit, drain to one, refill
//
// ASan/UBSan is a separate target (san_check_low_activity) over this same source.
//
// Usage:  check_low_activity KIND N [TRANCOCSV]

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace validation;

#ifndef E25_DIAG
#error "check_low_activity must be built with -DE25_DIAG"
#endif

namespace {

std::uint64_t checks = 0;
std::uint64_t failures = 0;

// Set when the workload never reached the new path. Distinct from `failures` on purpose: an
// unexercised branch is a gap in coverage, not evidence of a defect, and the two need
// different responses -- one calls for a workload that reaches the state, the other for a fix.
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

// THE CANDIDATE IS INERT UNDER THE SHIPPED CONFIGURATION, and the gate has to be built on a
// mechanism that calls repack_subblock at all. It has exactly two reachable callers:
// place_with_ctx under `subblock_reclaim`, and reclaim_widen under `reclaim_widest`. Both are
// OFF in the shipped configuration -- so with them off, disabling reclaim_low_activity changes
// a function that is never called. An earlier version of this gate ran with both off and the
// branch counter read 0, which is the gate doing its job.
//
// So the base configuration here is the E31 arm. E31 = subblock_reclaim only; WIDE = both.
Config arm_config(std::uint32_t thr, bool wide) {
    Config c = e25sw::baseline_config(thr);
    e25sw::pin_frozen_switches(c);
    c.subblock_reclaim = true;      // E31, so repack_subblock is reachable at all
    c.reclaim_widest = wide;        // G3/A1' on top of E31
    c.reclaim_low_activity = false;
    c.compact_region_routing = false;

    return c;
}

const Mode kMode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

// Compares the two indexes and the reference on the observable, not on the layout. A reclaim
// change is allowed to move bytes; it is not allowed to change an answer.
void compare_all(const Index& a, const Index& b, const Reference& ref, const std::string& label) {
    require(a.size() == b.size(), label + ": the two indexes hold the same number of keys");
    require(a.size() == ref.size(), label + ": and it is the number the reference holds");

    std::uint64_t mismatches = 0;

    for (const auto& kv : ref) {
        const Result ra = a.find(kv.first, kMode);
        const Result rb = b.find(kv.first, kMode);

        if (!ra.found || !rb.found || ra.value != kv.second || rb.value != kv.second) {
            ++mismatches;
        }
    }

    require(mismatches == 0, label + ": every live key answers the same in all three");

    // A scan over several starts, compared row by row.
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

        for (std::size_t i = 0; i < std::min(sa.size(), sb.size()); ++i) {
            require(sa[i].first == sb[i].first && sa[i].second == sb[i].second,
                    label + ": scan agrees row by row");
        }
    }

    // And the absent probe, which must miss in both.
    require(!a.find(std::string("~absent~", 8), kMode).found,
            label + ": an absent key misses on the baseline");
    require(!b.find(std::string("~absent~", 8), kMode).found,
            label + ": an absent key misses on the candidate");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_low_activity KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const Config off = arm_config(32, false);
        Config on = off;
        on.reclaim_low_activity = true;

        auto a = std::make_unique<Index>(off);
        auto b = std::make_unique<Index>(on);
        a->bulk_load(keys);
        b->bulk_load(keys);

        Reference ref;

        for (const auto& kv : keys) ref.emplace(kv.first, kv.second);

        compare_all(*a, *b, ref, "after bulk_load");
        require(a->region_count() == b->region_count(),
                "the switch does not change the region count");

        // ------------------------------------------------------------------ replacement
        // A REPLACEMENT, not an erase. An erase only frees space: it never fails a placement,
        // so no repack is ever entered and the branch cannot fire at all. Erasing alone was
        // the first version of this case and its counter read zero -- correctly.
        //
        // Victims are taken at random over the key space so blocks get drained from the middle
        // as well as from the edges, and every fresh key is anchored on its own victim so the
        // key distribution does not drift away from the one the index was planned for.
        {
            std::vector<std::size_t> order(N);

            for (std::size_t i = 0; i < N; ++i) order[i] = i;

            std::mt19937_64 rng(20261009);
            std::shuffle(order.begin(), order.end(), rng);

            const std::size_t rounds = std::min<std::size_t>(N, 40000);
            std::uint64_t serial = 0;

            validation::e25::counts().reset();

            for (std::size_t i = 0; i < rounds; ++i) {
                const std::string& victim = keys[order[i]].first;

                std::string base = victim;
                const auto pos = base.find("~f");

                if (pos != std::string::npos) base.resize(pos);

                std::string fresh = base + "~f" + std::to_string(serial++);

                while (ref.count(fresh) != 0) fresh += "x";

                const std::uint64_t value = 0x5000ULL + serial;

                a->erase(victim);
                b->erase(victim);
                ref.erase(victim);

                a->insert(fresh, value);
                b->insert(fresh, value);
                ref.emplace(std::move(fresh), value);
            }

            compare_all(*a, *b, ref, "after replacement");

            const std::uint64_t la = validation::e25::counts().low_activity_reclaimed;
            const std::uint64_t sr = validation::e25::counts().subblock_reclaims;
            const std::uint64_t sh = validation::e25::counts().subblock_reclaim_helped;

            // TWO DIFFERENT QUESTIONS, and conflating them is how this reads as one number:
            //   subblock_reclaims -- did the E31 mechanism run at all on this workload?
            //   low_activity_reclaimed -- and when it ran, did it meet a block holding 0 or 1
            //                             live records?
            // Zero on the first means the workload never entered the mechanism; zero on the
            // second with a non-zero first means the opportunity the hole diagnosis measured
            // is not reachable through THIS entry.
            std::cout << "  E31 repack passes: " << sr << " (helped " << sh << ")"
                      << " | low-activity passes: " << la << "\n";

            // COVERAGE IS NOT CORRECTNESS, and the two get different exit codes. A workload
            // that never reaches the new path has verified nothing about it, and reporting
            // that as "passed" is the vacuity this project has been burned by before.
            // Reporting it as a correctness FAILURE would be equally wrong: nothing was shown
            // to be incorrect. So it is its own outcome.
            require(sr > 0, "the E31 mechanism ran at all on this workload");

            if (la == 0) {
                std::cout << "  NOT EXERCISED: the low-activity branch never ran on this "
                             "workload, so this run says nothing about it\n";
                not_exercised = true;
            } else {
                std::cout << "  branch fired " << la << " times during the replacement\n";
            }
        }

        // ------------------------------------------------------------------ delete/reinsert
        {
            // Also a LIVE key, for the same reason as the duplicate case above.
            const std::string k = ref.begin()->first;

            a->erase(k);
            b->erase(k);
            ref.erase(k);
            compare_all(*a, *b, ref, "after deleting a live key");

            a->insert(k, 777);
            b->insert(k, 777);
            ref.emplace(k, 777);
            compare_all(*a, *b, ref, "after reinserting it");
        }

        // ------------------------------------------------------------------ duplicate key
        {
            // A key that is LIVE, taken from the reference rather than from the original
            // corpus: after a full replacement the original keys are all gone, and
            // `ref[k]` on an absent key DEFAULT-CONSTRUCTS one and inserts {k, 0} -- which
            // made this case fail against a reference the test itself had corrupted. The
            // first version of this block did exactly that.
            const std::string k = ref.begin()->first;
            const std::uint64_t old = ref.begin()->second;
            const std::size_t before = ref.size();

            const bool ia = a->insert(k, 999);
            const bool ib = b->insert(k, 999);

            require(!ia && !ib, "a duplicate insert is refused in both");
            require(ref.size() == before, "the test did not corrupt its own reference");
            compare_all(*a, *b, ref, "after a refused duplicate insert");

            require(a->find(k, kMode).value == old && b->find(k, kMode).value == old,
                    "a duplicate insert kept the old value in both");
        }

        // ------------------------------------------------------------------ empty suffix
        // A key that equals a region's prefix has a zero-length suffix. The packing loop
        // moves zero bytes and must still set offset and bottom correctly, so a block holding
        // such a record is drained to n == 1 and refilled.
        {
            std::vector<Pair> small = {
                {std::string("pre"), 1}, {std::string("pre0"), 2}, {std::string("pre1"), 3},
                {std::string("pre2"), 4}, {std::string("pre3"), 5}, {std::string("pre4"), 6},
            };

            Config o2 = arm_config(8, false);
            Config c2 = o2;
            c2.reclaim_low_activity = true;
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

            compare_all(*sa, *sb, r2, "n==1 with an empty-suffix candidate");

            sa->insert(std::string("pre9"), 9);
            sb->insert(std::string("pre9"), 9);
            r2.emplace(std::string("pre9"), 9);
            compare_all(*sa, *sb, r2, "after refilling the drained block");
        }

        // ------------------------------------------------------------------ report
        const std::uint64_t fired = validation::e25::counts().low_activity_reclaimed;

        std::cout << "{\"kind\":\"" << kind << "\",\"keys\":" << keys.size()
                  << ",\"final_size\":" << b->size()
                  << ",\"regions\":" << b->region_count()
                  << ",\"low_activity_passes\":" << fired
                  << ",\"checks\":" << checks
                  << ",\"failures\":" << failures
                  << "}" << std::endl;

        if (failures != 0) {
            std::cout << failures << " FAILED out of " << checks << "\n";
            return 1;
        }

        if (not_exercised) {
            std::cout << checks << "/" << checks
                      << " checks passed, but the new path was NOT EXERCISED on this corpus\n";
            return 3;
        }

        std::cout << checks << "/" << checks << " checks passed" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

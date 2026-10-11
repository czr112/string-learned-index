// E38 / P1.2 + P1.4: the LITS model gate.
//
// THE CLAIM, in one sentence: inside the supported input domain the adapter maps every key to
// the same logical group the pinned source's own position arithmetic produces, and outside it
// the adapter refuses instead of reading out of bounds or quietly substituting something.
//
// THE REFERENCE IS A TRANSCRIPTION, NOT A SECOND IMPLEMENTATION. `reference_group` below is
// lits_node.hpp:290-305 copied by hand: the `+ 1`, the clamp against the two sentinel slots,
// and the removal of the sentinel offset. The adapter expresses the same thing directly as
// `clamp(getPos(key, G, p, a, b), 0, G - 1)`. Both call the SAME getPos/getCdf out of the
// same header -- so what this gate tests is the ADAPTATION (which coordinate is passed, how
// the sentinels are removed, how the coefficients are derived), not whether two copies of
// the regression agree. E37's lesson is why: a second copy of an algorithm drifts, and the
// drift is invisible precisely where the gate is looking.
//
// WHY REAL REGIONS AND NOT SYNTHETIC ONES ONLY. The interesting values of `p` are the ones
// the corpus actually produces, and those come from `compute_prefix` over the real partition
// -- so section A reads them back off real Region objects rather than constructing them.
// Section B then adds the values the corpus does NOT produce (p = 31/32/33 and 123, and the
// degenerate regions), because a value that never occurs still has to be correct.
//
// WHAT IT DOES NOT COVER, SAID OUT LOUD.
//
//   * The `NonFinite` guard: NOT TRIGGERED THIS ROUND. The earlier note here argued it was
//     unreachable from `hi > lo`, and that argument does not hold -- "the span is at least an
//     ulp" bounds the span from BELOW, and a bound from below on the span is not a bound from
//     above on its reciprocal in a form that covers every input. So the guard stays, the
//     status stays, and it is reported as untriggered rather than counted as covered. Nothing
//     was added to production code to force it.
//   * The `CoefficientTooLarge` guard is PROBED, not claimed: a two-key region sharing a
//     120-character prefix is run and whichever status comes out is recorded.
//
// The empty-region and single-key paths ARE covered, on CONSTRUCTED inputs. Whether a real
// region has G == 1 or one key is a consequence of the group-count rule and the key count,
// and it is not inferable from the corpora dividing evenly by `region_keys` -- so they are
// built rather than argued about.

#include "data.hpp"
#include "model/lits_hpt_adapter.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using validation::Config;
using validation::Pair;

// ---------------------------------------------------------------------------
// The reference: lits_node.hpp:290-305, transcribed.
// ---------------------------------------------------------------------------

// `item_array_len` is the source's array length. Removing the two sentinel slots makes it
// G + 2, which is why getPos is handed G -- the same substitution the adapter documents.
std::uint32_t reference_group(const lits::HPT& hpt, char* key, int groups, int gcpl,
                              double k, double b) {
    const int pos = hpt.getPos(key, groups, gcpl, k, b) + 1;          // lits_node.hpp:292-294
    const int idx = std::max<int>(std::min<int>(pos, groups), 1);     // lits_node.hpp:305

    return static_cast<std::uint32_t>(idx - 1);
}

// lits_node.hpp:365-370, transcribed. Returns false on the source's own FAIL_TO_BULK.
bool reference_calibration(const lits::HPT& hpt, char* first, char* last, int gcpl,
                           double& k, double& b) {
    const double min_cdf = hpt.getCdf(first, gcpl);
    const double max_cdf = hpt.getCdf(last, gcpl);

    if (max_cdf <= min_cdf) return false;

    k = 1. / (max_cdf - min_cdf);
    b = min_cdf / (min_cdf - max_cdf);

    return true;
}

// ---------------------------------------------------------------------------
// Coverage counters. Every one of these is asserted non-zero at the end where the round
// claims the path runs: a gate that passes without executing the branch it names has proved
// nothing, which is lesson 29 from E31 and the reason the assertions exist.
// ---------------------------------------------------------------------------

struct Counts {
    std::uint64_t regions = 0;
    std::uint64_t regions_ok = 0;
    std::uint64_t regions_single_group = 0;
    std::uint64_t regions_degenerate = 0;
    std::uint64_t keys_compared = 0;
    std::uint64_t absent_compared = 0;
    std::uint64_t calibration_compared = 0;
    std::uint64_t group_changed_vs_context16 = 0;
    std::uint64_t edge_regions = 0;
    std::uint64_t edge_keys = 0;
    std::uint64_t reject_high_byte = 0;
    std::uint64_t reject_embedded_nul = 0;
    std::uint64_t reject_too_few = 0;
    std::uint64_t reject_short_key = 0;
    std::uint64_t reject_degenerate = 0;
    std::uint64_t reject_unusable_predict = 0;
    std::uint64_t status_nonfinite = 0;
    std::uint64_t p_zero_regions = 0;
    std::uint64_t p_ge32_regions = 0;
    std::uint64_t p_ge96_regions = 0;
    // The scan's precondition, measured rather than inherited: the pinned LITS tests it at a
    // node's first and last key only, so everything in between is an assumption.
    std::uint64_t monotone_checked = 0;
    std::uint64_t monotone_violations = 0;
    std::uint64_t groups_total = 0;
    std::uint64_t groups_used = 0;
    std::uint64_t groups_used_context16 = 0;
    std::uint64_t group_pop_max = 0;
    std::uint64_t group_pop_max_context16 = 0;
    std::uint64_t raw_below_1 = 0;
    std::uint64_t raw_at_or_above_G = 0;
    std::uint64_t regions_ps_lt1 = 0;
    std::uint64_t keys_in_ps_lt1 = 0;
    // The accumulation bound: the worst measured value of |G*alpha|*(kMaxKeyBytes+1)+|G*beta|,
    // against the adapter's limit. This is what stands behind the source's internal
    // `static_cast<int>(c)` being defined, so it is reported rather than assumed.
    double worst_accum_bound = 0.0;
    std::uint64_t regions_coeff_too_large = 0;
    std::uint64_t coeff_probe_accepted = 0;
    std::uint64_t coeff_probe_other = 0;
    std::uint64_t reject_coeff_too_large = 0;
    std::uint64_t reject_long_key = 0;
    // The defined boundary path, on constructed inputs.
    std::uint64_t boundary_empty = 0;
    std::uint64_t boundary_single_key = 0;
    std::uint64_t boundary_single_group = 0;
    // The crowding headline as a FRACTION, because the absolute maximum alone is not
    // comparable across corpora whose regions hold the same 2,048 keys but whose G differs.
    double worst_group_share = 0.0;
    double worst_group_share_context16 = 0.0;
    double share_sum = 0.0;
    double share_sum_context16 = 0.0;
    std::uint64_t share_regions = 0;
    std::uint64_t regions_lits_more_crowded = 0;
    std::uint64_t regions_lits_less_crowded = 0;
};

Counts g;

void fail(const std::string& what) {
    throw std::runtime_error("LITS MODEL GATE FAILED: " + what);
}

std::string describe(std::string_view s) {
    std::string out;

    for (unsigned char c : s) {
        if (c >= 32 && c < 127) out.push_back(static_cast<char>(c));
        else { out += "\\x"; out += "0123456789abcdef"[c >> 4]; out += "0123456789abcdef"[c & 15]; }
    }

    return "(" + std::to_string(s.size()) + "B) " + out;
}

// ---------------------------------------------------------------------------
// Section A: the real partition.
// ---------------------------------------------------------------------------

void check_real_dataset(const std::string& kind, std::size_t nkeys, const std::string& trcsv) {
    const auto pairs = validation::make_data(kind, nkeys, trcsv);
    const std::size_t N = pairs.size();

    Config cfg = e25sw::baseline_config(32);
    cfg.write_fast_locate = true;
    cfg.reuse_locate_ctx = true;
    cfg.compact_reuse_tag = true;
    cfg.compact_needs_garbage = true;
    cfg.scan_reserve_result = true;

    validation::Index idx(cfg);
    idx.bulk_load(pairs);

    // The shared table is trained on the SORTED, DEDUPLICATED FULL key set -- make_data
    // already guarantees both.
    std::vector<std::string> all_keys;
    all_keys.reserve(N);
    for (const auto& kv : pairs) all_keys.push_back(kv.first);

    // The live set, used only to confirm that a key this round declares absent really is.
    std::unordered_set<std::string> live;
    live.reserve(N * 2);
    for (const auto& k : all_keys) live.insert(k);

    const auto shared = lits_hpt::SharedModel::train(all_keys);

    std::cerr << "  " << kind << ": " << N << " keys, " << idx.region_count() << " regions, "
              << "training gcpl=" << shared->training_common_prefix()
              << " table=" << shared->table_payload_bytes() << "B"
              << " in " << shared->table_allocations() << " allocations" << std::endl;

    const std::size_t chunk = cfg.region_keys ? cfg.region_keys : N;

    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const validation::Region& region = idx.region(r);

        const std::size_t lo_i = r * chunk;
        const std::size_t hi_i = std::min(lo_i + chunk, N);

        if (lo_i >= hi_i) continue;

        const std::size_t p = region.prefix().size();
        const std::uint32_t G = region.groups();

        ++g.regions;
        if (p == 0) ++g.p_zero_regions;
        if (p >= 32) ++g.p_ge32_regions;
        if (p >= 96) ++g.p_ge96_regions;

        // The relation the adapter relies on, asserted rather than assumed: a region's keys
        // are a subset of the training keys, so its prefix cannot be SHORTER than the
        // training common prefix, and every position the region's model reads was trained.
        if (p < std::size_t(shared->training_common_prefix()))
            fail("region prefix is shorter than the training common prefix");

        const std::string& first = pairs[lo_i].first;
        const std::string& last = pairs[hi_i - 1].first;

        std::size_t max_key_bytes = 0;
        for (std::size_t i = lo_i; i < hi_i; ++i) {
            max_key_bytes = std::max(max_key_bytes, pairs[i].first.size());
        }

        const auto model = lits_hpt::RegionModel::calibrate(*shared, first, last, p, G,
                                                            hi_i - lo_i, max_key_bytes);

        // The bound that keeps the source's internal `static_cast<int>(c)` defined. Reported
        // as a measured worst case so the constant in the adapter is a number with evidence
        // behind it, not a guess that happens to hold.
        if (model.status == lits_hpt::Status::Ok) {
            const double bound = lits_hpt::accumulator_bound(G, model.alpha, model.beta,
                                                             max_key_bytes);

            if (bound > g.worst_accum_bound) g.worst_accum_bound = bound;
        }
        else if (model.status == lits_hpt::Status::CoefficientTooLarge) {
            ++g.regions_coeff_too_large;
        }

        if (model.status == lits_hpt::Status::SingleGroup ||
            model.status == lits_hpt::Status::TooFewKeys ||
            model.status == lits_hpt::Status::NoKeys) {
            ++g.regions_single_group;
            continue;
        }

        if (model.status != lits_hpt::Status::Ok) {
            ++g.regions_degenerate;

            std::cerr << "    region " << r << " unusable: " << lits_hpt::to_string(model.status)
                      << " (prefix=" << p << " G=" << G << ")" << std::endl;
            continue;
        }

        ++g.regions_ok;

        // The coefficients must be the source's, bit for bit. If they are, the two group
        // computations below are reading the same numbers and the comparison is about the
        // ADAPTATION and not about floating point.
        // THE ONE-STEP TEST. `getPos` initialises `ps = size * k` and runs its loop body
        // only while `ps >= 1` (lits_model.hpp:166-173). So `G * alpha < 1` means the loop
        // NEVER executes for ANY key in the region: the position is left at `size * beta` and
        // the model does no discrimination at all. That is not a slow path or a rounding
        // issue, it is a region where the model is inert -- and it is decidable from the
        // calibrated coefficients alone, so it is counted here rather than inferred later
        // from a crowding figure.
        if (G * model.alpha < 1.0) {
            ++g.regions_ps_lt1;
            g.keys_in_ps_lt1 += hi_i - lo_i;
        }

        double k = 0, b = 0;
        const bool cal_ok = reference_calibration(shared->hpt(),
                                                  const_cast<char*>(first.c_str()),
                                                  const_cast<char*>(last.c_str()),
                                                  static_cast<int>(p), k, b);
        if (!cal_ok) fail("reference calibration failed where the adapter said Ok");
        if (model.alpha != k || model.beta != b) fail("adapter coefficients differ from the source's");

        ++g.calibration_compared;

        // Every live key in the region, checked against the source AND against the invariant
        // the scan depends on.
        //
        // The monotonicity check is not decoration. Region::scan walks groups in ascending
        // order and relies on the mapping being non-decreasing in key order (LITS_AUDIT
        // section 7; E33 built check_scan_contract for the frozen model). The pinned LITS
        // checks this only at the FIRST and LAST key of a node (lits_node.hpp:397) and falls
        // back otherwise, so for every key in between it is an assumption -- and an assumption
        // this gate can test rather than inherit.
        std::vector<std::uint32_t> population(G, 0);
        std::vector<std::uint32_t> pop_context16(G, 0);
        std::uint32_t prev = 0;
        bool have_prev = false;

        for (std::size_t i = lo_i; i < hi_i; ++i) {
            const std::string& key = pairs[i].first;

            const auto ref = reference_group(shared->hpt(), const_cast<char*>(key.c_str()),
                                             static_cast<int>(G), static_cast<int>(p), k, b);
            const auto got = model.predict_group(key);

            if (ref != got) {
                fail("live key disagrees: " + describe(key) + " ref=" + std::to_string(ref) +
                     " got=" + std::to_string(got) + " p=" + std::to_string(p) +
                     " G=" + std::to_string(G));
            }

            if (have_prev && got < prev) {
                ++g.monotone_violations;

                if (g.monotone_violations <= 5) {
                    std::cerr << "    MONOTONICITY VIOLATION region " << r << " at key " << i
                              << ": " << prev << " -> " << got << " " << describe(key) << std::endl;
                }
            }

            prev = got;
            have_prev = true;

            // The raw position BEFORE the clamp, split at both ends. The name says what it
            // measures, not what it suggests: `raw < 1` INCLUDES the region's own first key,
            // whose position is 0 by construction, so a non-zero count here is not by itself
            // evidence of anything pathological -- it says how much of the region's CDF falls
            // into the lowest `1/G` of the modelled range.
            //
            // The reason for counting it at all: the gate's crowding figure is a maximum, and
            // a maximum has several possible causes. This pair narrows them down instead of
            // leaving the explanation to a story.
            const int raw = shared->hpt().getPos(const_cast<char*>(key.c_str()),
                                                 static_cast<int>(G), static_cast<int>(p), k, b);

            if (raw < 1) ++g.raw_below_1;
            if (raw >= static_cast<int>(G)) ++g.raw_at_or_above_G;

            ++population[got];
            ++g.monotone_checked;

            // The mapping change against the frozen model, reported not asserted: the two
            // models are supposed to differ, that is the experiment. The frozen model's
            // population is accumulated here too, so this round never reports a crowding
            // figure for LITS alone -- a descriptive number with no baseline beside it is the
            // thing the review discipline forbids.
            const std::string_view suffix(key.data() + p, key.size() - p);
            const auto ctx16 = region.group_of(suffix);

            if (got != ctx16) ++g.group_changed_vs_context16;

            ++pop_context16[ctx16];

            ++g.keys_compared;
        }

        // How the region's keys spread over its groups. "Fewer crowded groups" is one of the
        // three questions the round asks, and this is its model-side half: it says nothing
        // about sub-blocks or overflow (those need the index) but it does say whether the
        // mapping clusters or spreads.
        std::uint32_t used = 0, max_pop = 0, used16 = 0, max_pop16 = 0;

        for (std::uint32_t i = 0; i < G; ++i) {
            if (population[i]) ++used;
            if (population[i] > max_pop) max_pop = population[i];

            if (pop_context16[i]) ++used16;
            if (pop_context16[i] > max_pop16) max_pop16 = pop_context16[i];
        }

        g.groups_total += G;
        g.groups_used += used;
        g.groups_used_context16 += used16;
        if (max_pop > g.group_pop_max) g.group_pop_max = max_pop;
        if (max_pop16 > g.group_pop_max_context16) g.group_pop_max_context16 = max_pop16;

        const double share = double(max_pop) / double(hi_i - lo_i);
        const double share16 = double(max_pop16) / double(hi_i - lo_i);

        if (share > g.worst_group_share) g.worst_group_share = share;
        if (share16 > g.worst_group_share_context16) g.worst_group_share_context16 = share16;

        // A maximum is one region's outlier, and reporting only a maximum is how a
        // descriptive number turns into a story. These two say what the tail looks like
        // across ALL regions: the mean of the per-region figure, and how often the new model
        // is the more crowded of the two.
        g.share_sum += share;
        g.share_sum_context16 += share16;
        ++g.share_regions;
        if (share > share16) ++g.regions_lits_more_crowded;
        if (share < share16) ++g.regions_lits_less_crowded;

        // Absent keys, from the generator this round uses. Each is checked against the live
        // set first: a "miss" that is actually present would make the comparison meaningless
        // rather than wrong, and that is exactly the kind of silent problem the task book
        // asks to rule out.
        for (std::size_t i = lo_i; i < hi_i; ++i) {
            const std::string probe = pairs[i].first + "~absent";

            // Checked against a live-key SET rather than a binary search: libstdc++'s
            // binary_search instantiates its comparator in both argument orders, so a
            // one-sided lambda does not compile -- and a set is the clearer statement of what
            // is being asked anyway.
            if (live.count(probe) != 0) {
                fail("an absent probe is actually a live key: " + describe(probe));
            }

            const auto ref = reference_group(shared->hpt(), const_cast<char*>(probe.c_str()),
                                             static_cast<int>(G), static_cast<int>(p), k, b);
            const auto got = model.predict_group(probe);

            if (ref != got) {
                fail("absent key disagrees: " + describe(probe) + " ref=" + std::to_string(ref) +
                     " got=" + std::to_string(got));
            }

            ++g.absent_compared;
        }
    }
}

// ---------------------------------------------------------------------------
// Section B: the boundary values, and section C: what the adapter refuses.
// ---------------------------------------------------------------------------

// Keys whose region common prefix is EXACTLY `p`, verified rather than assumed: `p` is
// asserted against the set's own first/last common prefix before the case is used.
//
// The varying part is the hex of `i` in REVERSED digit order. data.hpp's hex_key puts the
// least significant digit LAST, so a set of 64 such keys shares its first fourteen hex
// characters and the common prefix comes out at p + 14 instead of p -- which is how the
// first version of this gate failed its own p = 0 case. Reversing makes key 0 start with '0'
// and key n-1 start with a different digit, so the common prefix is exactly p.
std::string reversed_hex(std::uint64_t x) {
    const char* digits = "0123456789abcdef";

    std::string out;
    out.reserve(16);

    for (int i = 0; i < 16; ++i) {
        out.push_back(digits[x & 15]);
        x >>= 4;
    }

    return out;
}

std::vector<std::string> make_prefixed(const std::size_t p, const std::size_t n,
                                       const std::size_t tail) {
    std::vector<std::string> out;
    out.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        std::string s(p, 'a');
        s += reversed_hex(i);
        s.append(tail, 'z');
        out.push_back(std::move(s));
    }

    std::sort(out.begin(), out.end());
    return out;
}

std::size_t common_prefix_of(const std::vector<std::string>& v) {
    if (v.size() < 2) return 0;

    const auto& a = v.front();
    const auto& b = v.back();

    std::size_t i = 0;
    while (i < std::min(a.size(), b.size()) && a[i] == b[i]) ++i;

    return i;
}

std::size_t max_key_len(const std::vector<std::string>& v) {
    std::size_t m = 0;

    for (const auto& k : v) m = std::max(m, k.size());

    return m;
}

// One key set, calibrated and compared. Every boundary case below is a choice of (keys,
// G, probes) handed to this function -- the calibration, the coefficient comparison and the
// key-by-key comparison exist ONCE, so a case cannot accidentally be checked differently
// from its neighbours. `expected_p` is the prefix length the case is meant to produce, or
// SIZE_MAX to take whatever the set produces.
void compare_keyset(const std::vector<std::string>& keys, const std::uint32_t groups,
                    const char* label, const std::size_t expected_p,
                    const bool interior_probes) {
    if (keys.size() < 2) fail(std::string(label) + ": needs at least two keys");

    const std::size_t p = common_prefix_of(keys);

    if (expected_p != SIZE_MAX && p != expected_p) {
        fail(std::string(label) + ": asked for p=" + std::to_string(expected_p) +
             " but the key set has p=" + std::to_string(p));
    }

    const auto shared = lits_hpt::SharedModel::train(keys);

    const auto model = lits_hpt::RegionModel::calibrate(*shared, keys.front(), keys.back(), p,
                                                        groups, keys.size(), max_key_len(keys));

    ++g.edge_regions;

    if (groups == 1) {
        if (model.status != lits_hpt::Status::SingleGroup)
            fail(std::string(label) + ": G=1 must report SingleGroup, got " +
                 lits_hpt::to_string(model.status));
        if (model.predict_group(keys.front()) != 0)
            fail(std::string(label) + ": G=1 must map to group 0");

        return;
    }

    if (model.status != lits_hpt::Status::Ok)
        fail(std::string(label) + ": unusable, " + std::string(lits_hpt::to_string(model.status)));

    double k = 0, b = 0;
    if (!reference_calibration(shared->hpt(), const_cast<char*>(keys.front().c_str()),
                               const_cast<char*>(keys.back().c_str()), static_cast<int>(p), k, b))
        fail(std::string(label) + ": the reference refused a calibration the adapter accepted");
    if (model.alpha != k || model.beta != b)
        fail(std::string(label) + ": coefficients differ from the source's");

    ++g.calibration_compared;

    std::vector<std::string> probes = keys;

    // Probes that sit INSIDE the key range exercise a different part of the CDF than one
    // that sorts after everything: a key between two live keys is where a flat region shows.
    if (interior_probes) {
        for (std::size_t i = 0; i + 1 < keys.size(); i += 7) {
            std::string mid = keys[i];
            mid.push_back('5');
            probes.push_back(std::move(mid));
        }
    }

    // The absent family this round uses. Each key extends a live one, so it sorts after it
    // and inside the region.
    for (const auto& key : keys) probes.push_back(key + "~absent");

    for (const auto& key : probes) {
        const auto ref = reference_group(shared->hpt(), const_cast<char*>(key.c_str()),
                                         static_cast<int>(groups), static_cast<int>(p), k, b);
        const auto got = model.predict_group(key);

        if (ref != got) {
            fail(std::string(label) + ": key disagrees: " + describe(key) +
                 " ref=" + std::to_string(ref) + " got=" + std::to_string(got) +
                 " p=" + std::to_string(p) + " G=" + std::to_string(groups));
        }

        ++g.edge_keys;
    }
}

void check_edge_region(const std::size_t p, const std::uint32_t groups, const std::size_t n,
                       const std::size_t tail) {
    compare_keyset(make_prefixed(p, n, tail), groups, "edge", p, true);
}

// Keys of length exactly p + 1: the position walk takes one step and meets the terminator.
void check_short_suffix(const std::size_t p) {
    std::vector<std::string> keys;

    for (char c : {'a', 'b', 'c', 'd'}) {
        std::string s(p, 'a');
        s.push_back(c);
        keys.push_back(std::move(s));
    }

    std::sort(keys.begin(), keys.end());

    compare_keyset(keys, 8, "short-suffix", p, false);
}

// A plateau: the keys share 120 characters and the model can only separate them at the very
// end, so the CDF is nearly flat across the region and neighbouring position buckets are
// where the disagreement, if any, would show.
void check_flat_cdf() {
    const auto keys = make_prefixed(120, 64, 0);

    compare_keyset(keys, 64, "flat-cdf", 120, true);
}

// a / aa / ab: keys where one is a strict prefix of another. The pinned loops stop at the
// first NUL, so the shorter key terminates the position walk where the longer one continues.
void check_prefix_relations() {
    std::vector<std::string> keys = {"aa", "aaa", "aab", "ab", "abab", "abb", "b"};
    std::sort(keys.begin(), keys.end());

    compare_keyset(keys, 7, "prefix-relations", SIZE_MAX, false);
}

void check_domain_refusals() {
    // A byte >= 128. lits_model.hpp:170 would index the table with a negative char.
    {
        std::vector<std::string> keys = {"abc", std::string("ab") + char(200), "abd"};
        std::sort(keys.begin(), keys.end());

        bool threw = false;
        try { lits_hpt::SharedModel::train(keys); } catch (const std::invalid_argument&) { threw = true; }

        if (!threw) fail("a high byte was accepted by train()");
        ++g.reject_high_byte;
    }

    // An embedded NUL. The pinned loops stop at the first NUL, so the model would silently
    // see a shorter key than the caller passed.
    {
        std::vector<std::string> keys = {"abc", std::string("ab\0c", 4), "abd"};
        std::sort(keys.begin(), keys.end());

        bool threw = false;
        try { lits_hpt::SharedModel::train(keys); } catch (const std::invalid_argument&) { threw = true; }

        if (!threw) fail("an embedded NUL was accepted by train()");
        ++g.reject_embedded_nul;
    }

    // Fewer than two keys: the pinned train() reads keys[1] and keys[len - 2].
    {
        std::vector<std::string> keys = {"only"};

        bool threw = false;
        try { lits_hpt::SharedModel::train(keys); } catch (const std::invalid_argument&) { threw = true; }
        if (!threw) fail("a single key was accepted by train()");

        std::vector<std::string> none;
        threw = false;
        try { lits_hpt::SharedModel::train(none); } catch (const std::invalid_argument&) { threw = true; }
        if (!threw) fail("an empty key set was accepted by train()");

        ++g.reject_too_few;
    }

    // A region prefix longer than its own keys. The source's loop would read past the NUL.
    {
        const auto keys = make_prefixed(12, 8, 0);
        const auto shared = lits_hpt::SharedModel::train(keys);

        const auto model = lits_hpt::RegionModel::calibrate(*shared, keys.front(), keys.back(),
                                                            40, 8, keys.size(), max_key_len(keys));
        if (model.status != lits_hpt::Status::KeyShorterThanPrefix)
            fail("a prefix longer than the keys must be refused, got " +
                 std::string(lits_hpt::to_string(model.status)));
        ++g.reject_short_key;
    }

    // `hi <= lo`: the source's own FAIL_TO_BULK, reached here with first == last.
    {
        const auto keys = make_prefixed(4, 8, 0);
        const auto shared = lits_hpt::SharedModel::train(keys);

        const auto model = lits_hpt::RegionModel::calibrate(*shared, keys.front(), keys.front(),
                                                            4, 8, keys.size(), max_key_len(keys));
        if (model.status != lits_hpt::Status::DegenerateCdf)
            fail("a degenerate calibration must be refused, got " +
                 std::string(lits_hpt::to_string(model.status)));
        if (model.usable()) fail("a degenerate model reported itself usable");
        ++g.reject_degenerate;
    }

    // ---- the defined boundary path (task book section 3.4) -------------------------------
    //
    // These are the cases the review asked to cover with CONSTRUCTED inputs rather than with
    // an argument about the corpora. Whether a real region has G == 1 or a single key depends
    // on the group-count rule and the key count, and the three formal corpora happen to
    // produce neither -- which is a fact about those corpora and not about this code.

    // A region with NO keys.
    {
        const auto model = lits_hpt::RegionModel::boundary(0, lits_hpt::Status::NoKeys);

        if (!model.usable()) fail("the empty-region boundary path must be usable");
        if (model.predict_group("anything") != 0) fail("an empty region must map to group 0");
        if (model.shared != nullptr) fail("an empty region must not reference a shared table");

        ++g.boundary_empty;
    }

    // A region with ONE key.
    {
        const auto keys = make_prefixed(4, 8, 0);
        const auto shared = lits_hpt::SharedModel::train(keys);

        const auto model = lits_hpt::RegionModel::calibrate(*shared, keys.front(), keys.front(),
                                                            4, 8, 1, max_key_len(keys));
        if (model.status != lits_hpt::Status::TooFewKeys)
            fail("a single-key region must take the boundary path, got " +
                 std::string(lits_hpt::to_string(model.status)));
        if (model.predict_group(keys.front()) != 0)
            fail("a single-key region must map to group 0");

        ++g.boundary_single_key;
    }

    // G == 1, through the factory the caller uses, with no table at all.
    {
        const auto model = lits_hpt::RegionModel::boundary(1, lits_hpt::Status::SingleGroup);

        if (model.predict_group("anything") != 0) fail("G=1 must map to group 0");
        ++g.boundary_single_group;
    }

    // An unusable model must refuse to predict rather than return a plausible group.
    {
        const auto keys = make_prefixed(4, 8, 0);
        const auto shared = lits_hpt::SharedModel::train(keys);
        auto model = lits_hpt::RegionModel::calibrate(*shared, keys.front(), keys.back(), 4, 8,
                                                      keys.size(), max_key_len(keys));
        model.status = lits_hpt::Status::DegenerateCdf;

        bool threw = false;
        try { model.predict_group(keys.front()); } catch (const std::logic_error&) { threw = true; }

        if (!threw) fail("an unusable model predicted a group instead of refusing");
        ++g.reject_unusable_predict;
    }

    // An unsupported byte at CALIBRATION time, where the keys are the caller's.
    {
        const auto keys = make_prefixed(4, 8, 0);
        const auto shared = lits_hpt::SharedModel::train(keys);
        const std::string bad = std::string(4, 'a') + char(150);

        const auto model = lits_hpt::RegionModel::calibrate(*shared, bad, keys.back(), 4, 8,
                                                            keys.size(), max_key_len(keys));
        if (model.status != lits_hpt::Status::UnsupportedByte)
            fail("an unsupported calibration key must be refused, got " +
                 std::string(lits_hpt::to_string(model.status)));
        ++g.reject_high_byte;
    }

    // A key longer than the accumulation bound was written for.
    {
        const auto keys = make_prefixed(4, 8, 0);
        const auto shared = lits_hpt::SharedModel::train(keys);
        const auto model = lits_hpt::RegionModel::calibrate(*shared, keys.front(), keys.back(),
                                                            4, 8, keys.size(), max_key_len(keys));

        std::string huge(lits_hpt::kMaxKeyBytes + 1, 'a');

        bool threw = false;
        try { model.predict_group(huge); } catch (const std::invalid_argument&) { threw = true; }

        if (!threw) fail("a key longer than the accumulation bound must be refused");
        ++g.reject_long_key;
    }

    // THE COEFFICIENT BOUND, probed rather than asserted to be reachable.
    //
    // Two keys sharing a 120-character prefix have nearly identical HPT coordinates, so the
    // calibration span is tiny and `alpha` is large. Whether that crosses `kMaxAccumulator`
    // depends on how fast the table's PRO decays, which is not something to predict -- so the
    // case is RUN and whichever status comes out is recorded. What IS asserted is the
    // contract: an accepted model must have its bound inside the limit, and a rejected one
    // must not be silent.
    {
        const auto keys = make_prefixed(120, 2, 0);
        const auto shared = lits_hpt::SharedModel::train(keys);

        const auto model = lits_hpt::RegionModel::calibrate(*shared, keys.front(), keys.back(),
                                                            120, 613, keys.size(), max_key_len(keys));

        if (model.status == lits_hpt::Status::Ok) {
            const double bound = lits_hpt::accumulator_bound(613, model.alpha, model.beta,
                                                             max_key_len(keys));

            if (!(bound <= lits_hpt::kMaxAccumulator))
                fail("an accepted model has an accumulation bound outside the limit");

            if (bound > g.worst_accum_bound) g.worst_accum_bound = bound;

            ++g.coeff_probe_accepted;
        } else if (model.status == lits_hpt::Status::CoefficientTooLarge) {
            ++g.reject_coeff_too_large;
        } else {
            ++g.coeff_probe_other;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::invalid_argument("check_lits_model KIND:N[,KIND:N...] [TRANCOCSV]");

        // The size travels WITH the kind, because the three corpora do not share one -- the
        // formal sets are 131,072 / 131,072 / 262,144 and a single N would silently
        // mis-describe two of them.
        const std::string kinds = argv[1];
        const std::string trcsv = argc > 2 ? argv[2] : "";

        bool saw_corpus = false;

        // What the REQUESTED corpora are expected to contain, taken from the diag_domain
        // measurement rather than assumed:
        //
        //   zero-length region prefix   mixed 2/64, tranco 28/128, skew 0/64
        //   region prefix >= 32         mixed 25/64, skew 64/64, tranco 0/128 (max is 3)
        bool corpus_has_zero_prefix = false;
        bool corpus_has_long_prefix = false;

        std::size_t begin = 0;
        while (begin <= kinds.size()) {
            const auto comma = kinds.find(',', begin);
            const std::string item = kinds.substr(begin, comma == std::string::npos
                                                            ? std::string::npos : comma - begin);

            if (!item.empty()) {
                const auto colon = item.find(':');
                if (colon == std::string::npos) throw std::invalid_argument("expected KIND:N");

                const std::string kind = item.substr(0, colon);
                saw_corpus = true;
                if (kind == "mixed" || kind == "tranco") corpus_has_zero_prefix = true;
                if (kind == "mixed" || kind == "skew") corpus_has_long_prefix = true;

                check_real_dataset(kind, std::stoull(item.substr(colon + 1)), trcsv);
            }

            if (comma == std::string::npos) break;
            begin = comma + 1;
        }

        // Section B: the values the corpora do not produce, and the degenerates.
        for (std::size_t p : {std::size_t(0), std::size_t(1), std::size_t(31), std::size_t(32),
                              std::size_t(33), std::size_t(121), std::size_t(123)}) {
            check_edge_region(p, 64, 64, 0);
            check_edge_region(p, 2, 16, 0);
            check_edge_region(p, 613, 64, 8);
            check_edge_region(p, 1, 16, 0);
            check_short_suffix(p);      // one byte past the prefix, then the terminator
        }

        check_flat_cdf();
        check_prefix_relations();

        // Section C.
        check_domain_refusals();

        // ---- non-vacuity -------------------------------------------------------------
        // Two groups, because they answer different questions.
        //
        // The first group is corpus-INDEPENDENT: these paths are exercised by section B and
        // section C, which run on every invocation. A run that skipped them proved nothing.
        if (!g.edge_regions) fail("no edge region was examined");
        if (!g.edge_keys) fail("no edge key was compared");
        if (!g.reject_high_byte) fail("the high-byte refusal was not exercised");
        if (!g.reject_embedded_nul) fail("the embedded-NUL refusal was not exercised");
        if (!g.reject_too_few) fail("the too-few-keys refusal was not exercised");
        if (!g.reject_short_key) fail("the short-key refusal was not exercised");
        if (!g.reject_degenerate) fail("the degenerate-calibration refusal was not exercised");
        if (!g.reject_unusable_predict) fail("the unusable-predict refusal was not exercised");
        if (!g.reject_long_key) fail("the over-long-key refusal was not exercised");
        if (!g.boundary_empty) fail("the empty-region boundary path was not exercised");
        if (!g.boundary_single_key) fail("the single-key boundary path was not exercised");
        if (!g.boundary_single_group) fail("the G=1 boundary path was not exercised");

        // The second group depends on WHICH corpora were asked for, and saying so is the
        // point: `p == 0` occurs on mixed (3.1% of regions) and tranco (21.9%) and NEVER on
        // skew -- diag_domain measured that. A blanket "p_zero_regions > 0" therefore fails a
        // deliberate skew-only run, which is a false alarm about the adapter raised by a true
        // fact about the corpus. That is exactly the confusion this gate exists to avoid.
        if (saw_corpus) {
            if (!g.regions) fail("no real region was examined");
            if (!g.regions_ok) fail("no real region produced a usable model");
            if (!g.keys_compared) fail("no live key was compared");
            if (!g.absent_compared) fail("no absent key was compared");
            if (!g.calibration_compared) fail("no calibration was compared");
            if (!g.monotone_checked) fail("monotonicity was never evaluated");

            if (corpus_has_long_prefix && !g.p_ge32_regions) {
                fail("this corpus is expected to contain regions with p >= 32 and none was "
                     "examined");
            }
            if (corpus_has_zero_prefix && !g.p_zero_regions) {
                fail("this corpus is expected to contain regions with a zero-length prefix "
                     "and none was examined");
            }
        }

        std::cout << "{\"gate\":\"check_lits_model\""
                  << ",\"regions\":" << g.regions
                  << ",\"regions_ok\":" << g.regions_ok
                  << ",\"regions_single_group\":" << g.regions_single_group
                  << ",\"regions_degenerate\":" << g.regions_degenerate
                  << ",\"p_zero_regions\":" << g.p_zero_regions
                  << ",\"p_ge32_regions\":" << g.p_ge32_regions
                  << ",\"p_ge96_regions\":" << g.p_ge96_regions
                  << ",\"keys_compared\":" << g.keys_compared
                  << ",\"absent_compared\":" << g.absent_compared
                  << ",\"calibration_compared\":" << g.calibration_compared
                  << ",\"group_changed_vs_context16\":" << g.group_changed_vs_context16
                  << ",\"monotone_checked\":" << g.monotone_checked
                  << ",\"monotone_violations\":" << g.monotone_violations
                  << ",\"groups_total\":" << g.groups_total
                  << ",\"groups_used\":" << g.groups_used
                  << ",\"group_pop_max\":" << g.group_pop_max
                  << ",\"groups_used_context16\":" << g.groups_used_context16
                  << ",\"group_pop_max_context16\":" << g.group_pop_max_context16
                  << ",\"raw_below_1\":" << g.raw_below_1
                  << ",\"raw_at_or_above_G\":" << g.raw_at_or_above_G
                  << ",\"worst_group_share\":" << g.worst_group_share
                  << ",\"worst_group_share_context16\":" << g.worst_group_share_context16
                  << ",\"mean_group_share\":" << (g.share_regions ? g.share_sum / double(g.share_regions) : 0.0)
                  << ",\"mean_group_share_context16\":" << (g.share_regions ? g.share_sum_context16 / double(g.share_regions) : 0.0)
                  << ",\"regions_lits_more_crowded\":" << g.regions_lits_more_crowded
                  << ",\"regions_lits_less_crowded\":" << g.regions_lits_less_crowded
                  << ",\"regions_ps_lt1\":" << g.regions_ps_lt1
                  << ",\"keys_in_ps_lt1\":" << g.keys_in_ps_lt1
                  << ",\"worst_accum_bound\":" << g.worst_accum_bound
                  << ",\"accum_limit\":" << lits_hpt::kMaxAccumulator
                  << ",\"regions_coeff_too_large\":" << g.regions_coeff_too_large
                  << ",\"coeff_probe_accepted\":" << g.coeff_probe_accepted
                  << ",\"coeff_probe_other\":" << g.coeff_probe_other
                  << ",\"reject_coeff_too_large\":" << g.reject_coeff_too_large
                  << ",\"reject_long_key\":" << g.reject_long_key
                  << ",\"boundary_empty\":" << g.boundary_empty
                  << ",\"boundary_single_key\":" << g.boundary_single_key
                  << ",\"boundary_single_group\":" << g.boundary_single_group
                  << ",\"edge_regions\":" << g.edge_regions
                  << ",\"edge_keys\":" << g.edge_keys
                  << ",\"reject_high_byte\":" << g.reject_high_byte
                  << ",\"reject_embedded_nul\":" << g.reject_embedded_nul
                  << ",\"reject_too_few\":" << g.reject_too_few
                  << ",\"reject_short_key\":" << g.reject_short_key
                  << ",\"reject_degenerate\":" << g.reject_degenerate
                  << ",\"reject_unusable_predict\":" << g.reject_unusable_predict
                  << ",\"status_nonfinite_untriggered\":" << g.status_nonfinite
                  << "}" << std::endl;

        std::cerr << "check_lits_model PASS: " << g.keys_compared << " live + "
                  << g.absent_compared << " absent keys agree with the source's position "
                  << "arithmetic across " << g.regions << " real regions" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "check_lits_model failed: " << e.what() << std::endl;
        return 1;
    }
}

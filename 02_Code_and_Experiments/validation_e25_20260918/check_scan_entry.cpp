// check_scan_entry -- the Stage A scan-entry COUNTER HAND-CHECK (2026-10-10).
//
// WHY THIS EXISTS. The scan-entry round added per-call counters for the scan start advance
// (adv_p_cmps / adv_p_skipped / adv_p_subblocks and the overflow twins) plus the output split
// (out_primary / out_overflow / out_bytes). A counter incremented on the wrong line still
// moves, and every table the round prints would then agree with itself while measuring
// something else. This gate pins the counters to the loops they name by recomputing the SAME
// quantities from the index CONTENTS -- per-sub-block counts, suffixes, overflow order -- and
// comparing number for number on a case small enough to count by hand.
//
// THE TINY CASE. Twelve keys sharing the prefix "k0", built under an EXPLICIT capacity plan:
// one group, four sub-blocks of two slots each, so at most eight records sit in the primary
// and the remaining four must land in the group's overflow container. The plan is passed in
// rather than derived because the point of the case is a SHAPE: an advance that steps over
// whole sub-blocks, an advance that stops inside one, an overflow advance that stops after a
// record or two, and one start that walks both containers to the end.
//
// WHAT IS COMPARED, for every (start, limit) below:
//   * adv_p_cmps / adv_p_skipped / adv_p_subblocks, and adv_o_cmps / adv_o_skipped,
//     recomputed by walking the sub-block counts and the suffixes in stored order;
//   * rows / out_primary / out_overflow, recomputed by the merge the scan contract describes;
//   * the returned rows themselves, against a std::map reference.
//
// The two advance invariants are asserted as well (skipped <= cmps <= skipped + 1), and the
// gate refuses to pass unless the case actually produced: a sub-block step, a skipped overflow
// record, an advance stopped by a FAILING comparison, and an advance stopped exactly at a
// container boundary. A case where those never happen would certify nothing about the loops
// the round is diagnosing.
//
// Counts only. No timer is read and no speed-up is claimed.
//
// Usage: check_scan_entry [--print]   (--print also prints one line per case, for freezing
//        new hand-derived expectations into the table below)

#include "data.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;

using Reference = std::map<std::string, std::uint64_t, Less>;

#ifndef E25_DIAG
#error "check_scan_entry must be built with -DE25_DIAG: it reads the scan-entry counters"
#endif

namespace {

std::uint64_t g_checks = 0, g_failures = 0;

void require(bool ok, const std::string& what) {
    ++g_checks;

    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

std::string show(std::string_view s) {
    std::string out;

    for (unsigned char c : s) {
        if (c >= 32 && c < 127) {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", c);
            out += buf;
        }
    }

    return out.empty() ? std::string("<empty>") : out;
}

struct Deltas {
    std::uint64_t p_cmps = 0, p_skip = 0, p_sub = 0, o_cmps = 0, o_skip = 0;
    std::size_t rows = 0, out_primary = 0, out_overflow = 0;
};

struct Before {
    std::uint64_t p_cmps, p_skip, p_sub, o_cmps, o_skip, out_primary, out_overflow;
};

Before snap() {
    const auto& c = e25::counts();
    return Before{c.scan_adv_p_cmps, c.scan_adv_p_skipped, c.scan_adv_p_subblocks,
                  c.scan_adv_ov_cmps, c.scan_adv_ov_skipped, c.scan_out_primary,
                  c.scan_out_overflow};
}

Deltas delta(const Before& b, const Before& a, std::size_t rows) {
    Deltas d;
    d.p_cmps = a.p_cmps - b.p_cmps;
    d.p_skip = a.p_skip - b.p_skip;
    d.p_sub = a.p_sub - b.p_sub;
    d.o_cmps = a.o_cmps - b.o_cmps;
    d.o_skip = a.o_skip - b.o_skip;
    d.out_primary = static_cast<std::size_t>(a.out_primary - b.out_primary);
    d.out_overflow = static_cast<std::size_t>(a.out_overflow - b.out_overflow);
    d.rows = rows;
    return d;
}

// The independent walk. It reads the contents through the diagnostic accessors and applies
// the documented advance + merge rules; it never reads a counter.
Deltas model_scan(const Region& reg, std::uint32_t first_group, bool matches,
                  const std::string& s, std::size_t limit) {
    Deltas d;
    const std::uint32_t G = reg.groups();
    std::size_t rows = 0;

    for (std::uint32_t g = first_group; g < G && rows < limit; ++g) {
        const std::size_t nb = reg.diag_subblocks(g);
        std::size_t blk = 0, a = 0, b = 0;
        std::vector<std::string> ov;
        reg.diag_ov_entries(g, ov);

        if (g == first_group && matches) {
            while (blk < nb) {
                const std::size_t cnt = reg.diag_subblock_count(g, blk);

                while (a < cnt) {
                    ++d.p_cmps;

                    if (!(compare(reg.diag_suffix(g, blk, a), s) < 0)) break;

                    ++d.p_skip;
                    ++a;
                }

                if (a < cnt) break;

                ++d.p_sub;
                ++blk;
                a = 0;
            }

            while (b < ov.size()) {
                ++d.o_cmps;

                if (!(compare(ov[b], s) < 0)) break;

                ++d.o_skip;
                ++b;
            }
        }

        while (rows < limit) {
            while (blk < nb && a >= reg.diag_subblock_count(g, blk)) {
                ++blk;
                a = 0;
            }

            const bool have_a = blk < nb;
            const bool have_b = b < ov.size();

            if (!have_a && !have_b) break;

            bool take_a = have_a;

            if (have_a && have_b) take_a = compare(reg.diag_suffix(g, blk, a), ov[b]) < 0;

            if (take_a) {
                ++rows;
                ++d.out_primary;
                ++a;
            } else {
                ++rows;
                ++d.out_overflow;
                ++b;
            }
        }
    }

    d.rows = rows;
    return d;
}

bool same(const Deltas& x, const Deltas& y) {
    return x.p_cmps == y.p_cmps && x.p_skip == y.p_skip && x.p_sub == y.p_sub &&
           x.o_cmps == y.o_cmps && x.o_skip == y.o_skip && x.rows == y.rows &&
           x.out_primary == y.out_primary && x.out_overflow == y.out_overflow;
}

std::string render(const Deltas& d) {
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "p_cmps=%llu p_skip=%llu p_sub=%llu o_cmps=%llu o_skip=%llu rows=%zu "
                  "prim=%zu ovf=%zu",
                  static_cast<unsigned long long>(d.p_cmps),
                  static_cast<unsigned long long>(d.p_skip),
                  static_cast<unsigned long long>(d.p_sub),
                  static_cast<unsigned long long>(d.o_cmps),
                  static_cast<unsigned long long>(d.o_skip), d.rows, d.out_primary,
                  d.out_overflow);
    return buf;
}

// THE HAND TABLE. Structure (printed and asserted above): one group, four sub-blocks of two
// slots, so the primary holds the eight smallest keys k000..k007 and the overflow container
// holds k008..k011. Every number below is countable on paper from that shape:
//
//   k000: the first primary record is not below the suffix "00", so the advance evaluates
//         ONE comparison and skips nothing (cmps = skipped + 1). The overflow head behaves
//         the same way. The scan then emits all twelve rows: 8 primary, 4 overflow.
//   k004: sub-blocks 0 and 1 lie entirely below "04" -- two whole sub-block steps, four
//         skipped records -- and the failing comparison lands on sub-block 2's first record:
//         5 comparisons = 4 skips + 1.
//   k008: every primary record is below "08" (8 skips, 4 sub-block steps) and the walk runs
//         off the END of the container, so no failing comparison is evaluated there:
//         cmps == skipped == 8. The overflow head IS the start key, so 1 comparison, 0 skips,
//         and only the four overflow rows remain.
//   k011: the same primary walk; the overflow walk skips the three records below the start and
//         stops on the fourth: 4 comparisons = 3 skips + 1, and one row is left.
//   k0:   the empty suffix stops immediately in each container (1 comparison, 0 skips) and the
//         primary alone holds the first eight rows.
//   k1:   greater than the region prefix, so the early exit runs no loop at all: all zeros.
//
// These are the "hand-computed advance counts" the plan asks for: they are derived from the
// printed shape, not read back from the counters, and they are asserted on every run.
struct HandRow {
    const char* start;
    std::size_t limit;
    Deltas want;
};

const HandRow HAND[] = {
    {"k000", 32, {1, 0, 0, 1, 0, 12, 8, 4}},
    {"k004", 8, {5, 4, 2, 1, 0, 8, 4, 4}},
    {"k008", 4, {8, 8, 4, 1, 0, 4, 0, 4}},
    {"k011", 4, {8, 8, 4, 4, 3, 1, 0, 1}},
    {"k0", 8, {1, 0, 0, 1, 0, 8, 8, 0}},
    {"k1", 4, {0, 0, 0, 0, 0, 0, 0, 0}},
};

}  // namespace

int main(int argc, char** argv) {
    try {
        const bool print = (argc > 1 && std::string(argv[1]) == "--print");

        Config cfg = e25sw::baseline_config(32);
        e25sw::pin_frozen_switches(cfg);
        // derive_groups() adds one group to its estimate, so a 12-key region would get two
        // groups by default. The tiny case wants ONE group whose whole shape a reader can
        // count, so the estimate is pinned here, before the plan below is built for it.
        cfg.fixed_groups = 1;

        std::vector<Pair> keys;

        for (int i = 0; i < 12; ++i) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "k0%02d", i);
            keys.emplace_back(std::string(buf), static_cast<std::uint64_t>(100 + i));
        }

        for (std::size_t i = 1; i < keys.size(); ++i)
            require(compare(keys[i - 1].first, keys[i].first) < 0,
                    "the tiny key set must be in unsigned-byte order");

        // ONE group, FOUR sub-blocks, TWO slots each, 64 bytes of block budget: eight primary
        // slots against twelve keys, so four records must land in overflow. Nothing else in
        // this file is allowed to be "about" a larger machine -- the whole point is that a
        // reader can count this by hand.
        std::vector<std::vector<std::uint16_t>> sg{{2}};
        std::vector<std::vector<std::uint32_t>> bb{{128}};   // > 64B header + 2 slots, and a multiple of 64
        std::vector<std::vector<std::uint16_t>> caps{{4}};

        Index idx(cfg);
        idx.bulk_load_with_plan(keys, sg, bb, caps);

        require(idx.region_count() == 1, "the tiny case must build exactly one region");

        const Region& reg = idx.region(0);
        const std::size_t prefix_len = reg.diag_prefix_size();
        const std::string prefix = keys.front().first.substr(0, prefix_len);

        std::cout << "tiny case: 12 keys, prefix=" << show(prefix) << " (" << prefix_len
                  << " bytes), groups=" << reg.groups() << ", sub-block counts:";

        for (std::uint32_t g = 0; g < reg.groups(); ++g) {
            std::cout << " g" << g << "[";

            for (std::size_t blk = 0; blk < reg.diag_subblocks(g); ++blk)
                std::cout << (blk ? "," : "") << reg.diag_subblock_count(g, blk);

            std::cout << "]";
        }

        std::vector<std::size_t> ovs;
        idx.diag_overflow_sizes(ovs);
        std::cout << ", overflow sizes:";

        for (std::size_t n : ovs) std::cout << " " << n;

        std::cout << "\n";

        require(prefix_len == 2, "the tiny case's prefix must be the shared k0");
        require(reg.groups() == 1, "the tiny case must build exactly one group");
        require(reg.diag_subblocks(0) == 4, "the tiny case must build four sub-blocks");
        require(idx.overflow_count() >= 1, "the tiny case must place records in overflow");

        Reference ref;

        for (const auto& kv : keys) ref[kv.first] = kv.second;

        std::vector<std::pair<std::string, std::string>> starts;
        starts.emplace_back("empty", "");
        starts.emplace_back("before_prefix", "k");
        starts.emplace_back("prefix_only", "k0");

        for (const auto& kv : keys) starts.emplace_back("key_" + kv.first.substr(2), kv.first);

        starts.emplace_back("between_05_06", std::string("k005\x01"));
        starts.emplace_back("after_last", "k1");
        starts.emplace_back("far_after", "z");

        const std::size_t limits[] = {1, 4, 8, 32};

        std::uint64_t saw_subblock_step = 0, saw_ov_skip = 0, saw_stop_by_cmp = 0,
                     saw_stop_at_boundary = 0, hand_rows = 0;

        for (const auto& st : starts) {
            const bool matches = st.second.size() >= prefix_len &&
                                 (!prefix_len ||
                                  std::memcmp(st.second.data(), prefix.data(), prefix_len) == 0);
            std::uint32_t first_group = 0;

            if (matches) {
                const auto where = idx.diag_where(st.second);
                require(where.first == 0, "the tiny case has exactly one region");
                first_group = where.second;
            }

            const bool early_out = !matches && compare(st.second, prefix) > 0;
            const std::string s = matches ? st.second.substr(prefix_len) : std::string();

            for (std::size_t limit : limits) {
                const Before b0 = snap();
                const auto out = idx.scan(st.second, limit);
                const Deltas act = delta(b0, snap(), out.size());

                Deltas exp;

                if (!early_out) exp = model_scan(reg, first_group, matches, s, limit);

                const std::string label = st.first + " [" + show(st.second) + "]";

                if (!same(act, exp))
                    require(false, "counter mismatch at " + label + " limit=" +
                                       std::to_string(limit) + "\n  actual   " + render(act) +
                                       "\n  expected " + render(exp));

                for (const auto& h : HAND) {
                    if (h.limit != limit || st.second != h.start) continue;

                    ++hand_rows;
                    require(same(act, h.want),
                            std::string("hand table mismatch at ") + h.start + " limit=" +
                                std::to_string(h.limit) + "\n  actual   " + render(act) +
                                "\n  expected " + render(h.want));
                }

                if (act.p_sub > 0) ++saw_subblock_step;

                if (act.o_skip > 0) ++saw_ov_skip;

                if (act.p_cmps == act.p_skip + 1 || act.o_cmps == act.o_skip + 1)
                    ++saw_stop_by_cmp;

                if ((act.p_cmps > 0 && act.p_cmps == act.p_skip) ||
                    (act.o_cmps > 0 && act.o_cmps == act.o_skip))
                    ++saw_stop_at_boundary;

                // The answer itself, against the reference map, row by row.
                auto it = ref.lower_bound(st.second);
                std::size_t want = 0;

                for (auto t = it; t != ref.end() && want < limit; ++t, ++want) {
                    if (want >= out.size()) {
                        require(false, "scan returned fewer rows than the reference at " + label);
                        break;
                    }

                    if (out[want].first != t->first || out[want].second != t->second) {
                        require(false, "row mismatch at " + label + " row " +
                                           std::to_string(want));
                        break;
                    }
                }

                if (out.size() != want)
                    require(false, "scan returned " + std::to_string(out.size()) +
                                       " rows, reference wants " + std::to_string(want) +
                                       " at " + label + " limit=" + std::to_string(limit));

                if (print)
                    std::cout << "  " << label << " limit=" << limit << "  " << render(act)
                              << "\n";
            }
        }

        require(hand_rows == sizeof(HAND) / sizeof(HAND[0]),
                "every hand-table row must be exercised by the start/limit sweep");
        require(saw_subblock_step > 0, "no case stepped over a whole sub-block");
        require(saw_ov_skip > 0, "no case skipped an overflow record");
        require(saw_stop_by_cmp > 0, "no advance was stopped by a FAILING comparison");
        require(saw_stop_at_boundary > 0, "no advance ran to the end of its container");

        // ======================= candidate S: the ordered start advance =======================
        //
        // The S switch (Config::scan_ordered_start) replaces the linear cursor walk with an
        // ordered search that lands on the SAME position. This section pins that claim two
        // ways: the two modes must return IDENTICAL rows, and the records-passed counters
        // (p_skip / p_sub / o_skip) must be identical while the comparison counters drop --
        // "same records passed, fewer comparisons", measured rather than argued. The same
        // sweep runs again on a state with an EMPTIED sub-block in the middle, which is the
        // case the block-level search's left-walk exists for.
        {
            const auto ceil_log2 = [](std::size_t n) {
                std::size_t k = 0, v = 1;
                while (v < n + 2) { v <<= 1; ++k; }
                return k;
            };

            std::vector<std::pair<std::string, std::string>> starts_s = starts;
            starts_s.emplace_back("nul_suffix", std::string("k0\x00", 3));
            starts_s.emplace_back("high_suffix", std::string("k0\xff\x01"));
            starts_s.emplace_back("above_all", "k0~");

            Config cfgL = cfg;
            Config cfgS = cfg;
            cfgS.scan_ordered_start = true;

            require(!cfgL.scan_ordered_start, "the linear arm must not have S on");

            Index idxL(cfgL);
            idxL.bulk_load_with_plan(keys, sg, bb, caps);
            Index idxS(cfgS);
            idxS.bulk_load_with_plan(keys, sg, bb, caps);

            std::uint64_t s_cases = 0, s_fewer = 0, s_notfound = 0, s_ov_multi = 0,
                          s_empty_step = 0;

            const auto sweep_s = [&](Index& lin, Index& ord, bool with_empty) {
                for (const auto& st : starts_s) {
                    for (std::size_t limit : limits) {
                        const Before l0 = snap();
                        const auto outL = lin.scan(st.second, limit);
                        const Deltas dl = delta(l0, snap(), outL.size());

                        const Before s0 = snap();
                        const auto outS = ord.scan(st.second, limit);
                        const Deltas ds = delta(s0, snap(), outS.size());

                        const std::string label = st.first + " [" + show(st.second) + "]" +
                                                  (with_empty ? " (empty-block state)" : "");

                        require(outL.size() == outS.size(),
                                "S changed the ROW COUNT at " + label + " limit=" +
                                    std::to_string(limit));

                        for (std::size_t i = 0; i < outL.size() && i < outS.size(); ++i)
                            require(outL[i].first == outS[i].first &&
                                        outL[i].second == outS[i].second,
                                    "S changed a ROW at " + label + " row " + std::to_string(i));

                        require(dl.p_skip == ds.p_skip,
                                "S changed the primary skipped count at " + label +
                                    "\n  linear " + render(dl) + "\n  ordered " + render(ds));
                        require(dl.p_sub == ds.p_sub,
                                "S changed the passed-sub-block count at " + label +
                                    "\n  linear " + render(dl) + "\n  ordered " + render(ds));
                        require(dl.o_skip == ds.o_skip,
                                "S changed the overflow skipped count at " + label +
                                    "\n  linear " + render(dl) + "\n  ordered " + render(ds));

                        require(ds.p_cmps <= 4 + 2 * ceil_log2(reg.diag_subblocks(0)),
                                "the ordered primary search did more comparisons than its "
                                "logarithmic bound at " + label + " " + render(ds));
                        require(ds.o_cmps <= 1,
                                "the ordered overflow search must count one search per call at " +
                                    label + " " + render(ds));

                        if (ds.p_cmps + ds.o_cmps < dl.p_cmps + dl.o_cmps) ++s_fewer;

                        if (dl.p_sub >= reg.diag_subblocks(0) && ds.p_sub >= reg.diag_subblocks(0))
                            ++s_notfound;

                        if (dl.o_skip >= 2) ++s_ov_multi;

                        if (with_empty && dl.p_sub > 1) ++s_empty_step;

                        ++s_cases;
                    }
                }
            };

            sweep_s(idxL, idxS, false);

            // The emptied-sub-block state: kill sub-block 1 of the primary in BOTH copies.
            idxL.erase("k002");
            idxL.erase("k003");
            idxS.erase("k002");
            idxS.erase("k003");

            require(idxS.region(0).diag_subblock_count(0, 1) == 0,
                    "the emptied sub-block must stay empty (erase rebuilt the region?)");
            require(idxL.region(0).diag_subblock_count(0, 1) == 0,
                    "both copies must show the same emptied sub-block");

            sweep_s(idxL, idxS, true);

            require(s_fewer > 0, "no case showed the ordered search doing FEWER comparisons "
                                 "(the benefit branch never ran)");
            require(s_notfound > 0, "no case exercised the walk-off-the-end path");
            require(s_ov_multi > 0, "no case skipped multiple overflow records");
            require(s_empty_step > 0, "no case stepped over the emptied sub-block");

            std::cout << "ordered-start (S): " << s_cases << " comparisons of linear vs ordered"
                      << " (" << s_fewer << " with fewer comparisons, " << s_notfound
                      << " walk-offs, " << s_ov_multi << " multi-skip overflow, " << s_empty_step
                      << " over the emptied sub-block)\n";
        }

        std::cout << "check_scan_entry: " << g_checks << " checks, " << g_failures
                  << " failures";
        std::cout << " (sub-block steps=" << saw_subblock_step
                  << ", overflow skips=" << saw_ov_skip
                  << ", stopped-by-cmp=" << saw_stop_by_cmp
                  << ", stopped-at-boundary=" << saw_stop_at_boundary << ")\n";
        return g_failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
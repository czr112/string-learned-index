// P2 / A4 entry diagnosis: WHERE does a scan spend its count, and does it scale with the
// result?
//
// READ-ONLY BY CONSTRUCTION. This program implements no cursor, adds no switch, and changes
// no scan behaviour: it resets the diagnostic counters around an unchanged `Index::scan`,
// records them, and checks every result against the reference map. Task book section A4
// allows a cursor prototype ONLY if a diagnosis first shows rebuilding cost -- so the first
// thing to produce is the diagnosis.
//
// TWO CALIBERS, KEPT APART ON PURPOSE:
//   * absolute counts per scan        -- what the call actually did
//   * counts per RETURNED RECORD      -- what the result cost
// A quantity that does not grow with the result is SETUP PER CALL; one that does is WORK THE
// RESULT FORCED. The plan's suspicion (repeated locate, re-derived group, rebuilt cursors)
// is a claim about the first kind, so the two are never mixed into one number.
//
// ZERO-RETURN SCANS ARE LISTED SEPARATELY and never divided. A scan that returns nothing has
// no per-record figure at all, and folding those into an average would make the ratio depend
// on how many empty scans happened to be sampled.
//
// COUNTS LOCATE CANDIDATES; THEY DO NOT CONVERT TO TIME. Nothing here estimates a speedup.
//
// Output: results/main/scan_nav_<kind>.jsonl, one record per scan, plus a summary on stdout.
//
// Usage:  diag_scan_nav KIND N [TRANCOCSV]

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
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
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

#ifndef E25_DIAG
#error "diag_scan_nav.cpp needs -DE25_DIAG: the counters are diagnostic-only"
#endif

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

// A start key can contain a control byte on purpose (the "just past a present key" probe
// uses 0x01). Written raw it makes the record invalid JSON -- the first version of this
// program did exactly that and the file could not be parsed. Escape per JSON rules.
static std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);

    for (unsigned char c : s) {
        if (c == '\\') {
            o += "\\\\";
        } else if (c == '"') {
            o += "\\\"";
        } else if (c < 0x20) {
            static const char* HEX = "0123456789abcdef";
            o += "\\u00";
            o += HEX[(c >> 4) & 0xF];
            o += HEX[c & 0xF];
        } else {
            o += static_cast<char>(c);
        }
    }

    return o;
}

// The reference answer: from the first key >= start, at most `count`, ascending.
static std::vector<Pair> ref_scan(const Reference& ref, const std::string& start, std::size_t count) {
    std::vector<Pair> out;
    std::size_t got = 0;

    for (auto it = ref.lower_bound(start); it != ref.end() && got < count; ++it, ++got)
        out.emplace_back(it->first, it->second);

    return out;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_scan_nav KIND N [TRANCOCSV]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

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

        Config cfg = e25sw::baseline_config(thr);
        e25sw::pin_frozen_switches(cfg);

        Pipeline plan;
        {
            Index probe(cfg);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        auto idx = std::make_unique<Index>(cfg);
        idx->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        Reference ref;
        for (const auto& kv : keys) ref[kv.first] = kv.second;

        // The starts are the ones the existing scan work uses: the first key, the last key,
        // a key strictly beyond the last (which returns NOTHING -- the zero-return case), the
        // empty start, every key (present), and a stride sample of absent keys.
        const auto starts_for = [&]() {
            std::vector<std::string> out;
            std::unordered_set<std::string> seen;
            auto add = [&](const std::string& k) { if (seen.insert(k).second) out.push_back(k); };

            add(std::string());
            add(keys.front().first);
            add(keys.back().first);
            add(keys.back().first + "\x01");          // returns 0 records

            const std::size_t stride = std::max<std::size_t>(1, keys.size() / 256);
            for (std::size_t i = 0; i < keys.size(); i += stride) {
                add(keys[i].first);                    // present
                add(keys[i].first + "\x01");           // absent, just past a present key
            }

            return out;
        };

        const std::size_t LENS[3] = {16, 128, 1024};
        const char* STATES[3] = {"initial", "pct25", "pct200"};
        const double CPS[3] = {0.0, 0.25, 2.00};

        std::ofstream out(("results/main/scan_nav_" + kind + ".jsonl").c_str(), std::ios::trunc);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        std::size_t serial = 0;
        std::size_t checked = 0;
        std::size_t zero_return_scans = 0;

        // Per (state, n): summed counters and summed returned records, for the aggregate.
        struct Agg {
            std::uint64_t scans = 0, returned = 0;
            std::uint64_t locates = 0, canon = 0, regions = 0, groups = 0;
            std::uint64_t cursors = 0, ov_cursors = 0, blocks_skipped = 0;
            std::uint64_t merge = 0, allocs = 0, dir_rebuilds = 0;
        };
        std::map<std::pair<int, std::size_t>, Agg> agg;

        const Mode qm{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        for (int si = 0; si < 3; ++si) {
            // Advance to this state with the replacement sequence every other round uses:
            // erase a live key, insert a different one, live count constant.
            if (CPS[si] > 0) {
                std::deque<std::string> live;
                std::unordered_set<std::string> live_set;
                for (const auto& kv : keys) { live.push_back(kv.first); live_set.insert(kv.first); }

                const std::size_t target = static_cast<std::size_t>(double(N) * CPS[si]);

                for (std::size_t i = 0; i < target; ++i) {
                    const std::string victim = live.front();
                    live.pop_front();
                    live_set.erase(victim);

                    std::string base = victim;
                    const auto pos = base.find("~m");
                    if (pos != std::string::npos) base.resize(pos);

                    std::string fresh = base + "~m" + std::to_string(serial++);
                    while (ref.find(fresh) != ref.end() || !live_set.insert(fresh).second)
                        fresh += "x";

                    live.push_back(fresh);
                    idx->erase(victim);
                    idx->insert(fresh, 0xD00DULL + i);
                    ref.erase(victim);
                    ref[fresh] = 0xD00DULL + i;
                }
            }

            const auto starts = starts_for();

            for (std::size_t li = 0; li < 3; ++li) {
                const std::size_t len = LENS[li];

                for (const auto& st : starts) {
                    auto& C = validation::e25::counts();
                    C.reset();

                    const auto got = idx->scan(st, len);

                    const std::uint64_t locates = C.scan_start_locates;
                    const std::uint64_t canon = C.scan_canonical_group;
                    const std::uint64_t regions = C.scan_regions;
                    const std::uint64_t groups = C.scan_groups;
                    const std::uint64_t cursors = C.scan_cursor_starts;
                    const std::uint64_t ov_cursors = C.scan_ov_cursor_starts;
                    const std::uint64_t skipped = C.scan_blocks_skipped;
                    const std::uint64_t merge = C.scan_merge_cmps;
                    const std::uint64_t allocs = C.scan_result_allocs;

                    // The result must be what the reference says, or the counts describe a
                    // scan that did the wrong thing.
                    const auto want = ref_scan(ref, st, len);

                    if (got.size() != want.size())
                        throw std::runtime_error("scan returned a different number of records");

                    for (std::size_t i = 0; i < got.size(); ++i) {
                        if (got[i].first != want[i].first || got[i].second != want[i].second)
                            throw std::runtime_error("scan content differs from the reference");
                        ++checked;
                    }

                    if (got.empty()) ++zero_return_scans;

                    auto& a = agg[{si, len}];
                    a.scans += 1;
                    a.returned += got.size();
                    a.locates += locates;
                    a.canon += canon;
                    a.regions += regions;
                    a.groups += groups;
                    a.cursors += cursors;
                    a.ov_cursors += ov_cursors;
                    a.blocks_skipped += skipped;
                    a.merge += merge;
                    a.allocs += allocs;

                    // Escaped, and "<empty>" is reserved for the empty start so an empty
                    // string is never confused with a missing field.
                    const std::string st_esc =
                        st.empty() ? std::string("<empty>") : json_escape(st);

                    out << "{\"kind\":\"" << kind << "\",\"state\":\"" << STATES[si]
                        << "\",\"n\":" << len
                        << ",\"start\":\"" << st_esc << "\""
                        << ",\"returned\":" << got.size()
                        << ",\"locates\":" << locates
                        << ",\"canonical_group\":" << canon
                        << ",\"regions\":" << regions
                        << ",\"groups\":" << groups
                        << ",\"cursor_starts\":" << cursors
                        << ",\"ov_cursor_starts\":" << ov_cursors
                        << ",\"blocks_skipped\":" << skipped
                        << ",\"merge_cmps\":" << merge
                        << ",\"result_allocs\":" << allocs
                        << "}" << std::endl;
                }
            }
        }

        // ---- summary ---------------------------------------------------------------
        std::cout << "{\"kind\":\"" << kind << "\",\"case\":\"totals\","
                  << "\"scans_checked\":" << checked
                  << ",\"zero_return_scans\":" << zero_return_scans << "}" << std::endl;

        const char* COLS[9] = {"scans", "returned", "locates", "canonical_group", "regions",
                               "cursor_starts", "ov_cursor_starts", "merge_cmps", "result_allocs"};

        for (const auto& kv : agg) {
            const int si = kv.first.first;
            const std::size_t len = kv.first.second;
            const Agg& a = kv.second;

            const std::uint64_t vals[9] = {a.scans, a.returned, a.locates, a.canon, a.regions,
                                           a.cursors, a.ov_cursors, a.merge, a.allocs};

            std::cout << "  " << STATES[si] << " n=" << len << "  ";
            for (int c = 0; c < 9; ++c) std::cout << COLS[c] << "=" << vals[c] << " ";
            std::cout << std::endl;

            // Per returned record, printed only when there IS a result to divide by.
            if (a.returned > 0) {
                std::cout << "      per returned record:";
                for (int c = 2; c < 9; ++c)
                    std::cout << " " << COLS[c] << "="
                              << (double(vals[c]) / double(a.returned));
                std::cout << std::endl;
            } else {
                std::cout << "      per returned record: NOT COMPUTED (no records returned)"
                          << std::endl;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}

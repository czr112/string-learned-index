// check_block_model_backend.cpp -- P2 correctness entry for the BlockLinearRank backend.
//
// PLAN. docs/NEXT_ACTION_PLAN_20261011_BLOCK_MODEL.md sections 3 and 5. This program is a
// COUNTING gate: it takes no timings that a conclusion may be drawn from, and it produces no
// ranking. The only clocks in it are the two labelled build-cost numbers the plan asks the
// entry to record; P3 owns every speed claim.
//
// PRE-REGISTERED GATES. A cell is accepted only when all of them hold. The Python runner
// self-tests its own analyzer against deliberately broken records before it builds anything.
//
//   gate_load              the index built and reports the reference row count
//   gate_hits              every key finds its own value through Index::find
//   gate_miss              probe keys that are ABSENT in the reference answer absent
//   gate_partition         locate() is non-decreasing over the sorted keys
//   gate_domain            empty / NUL / high-byte / long / shared-prefix keys match the reference
//   gate_boundary          the DEFINED boundary cases are the declared ones (0 keys, 1 key, 1 group)
//   gate_dynamic           an insert/erase/update/split program matches the reference at every
//                          checkpoint and at the end
//   gate_negative_control  the checker's own comparisons reject deliberately wrong inputs
//
// Rank-specific gates: REQUIRED on the BlockLinearRank arm, NOT APPLICABLE (JSON null) on the
// Context16 control, which shares no rank model with it.
//
//   gate_rank_prefix     the accepted-prefix test pins the prefix the checker derived itself
//   gate_rank_monotone   placement_group() is non-decreasing over every region's own keys
//   gate_rank_range      every answer is inside [0, groups)
//   gate_rank_recompute  a RegionModel the CHECKER fits from the same range answers identically
//                        for every key (catches a different prefix, a different group count, or a
//                        mismatch between the fitted object and the one in use)
//   gate_stats_replay    the single-block demand replay's group ids equal placement_group()
//   gate_coverage        BlockRankStats reconciles with the live region and record counts
//
// WHY gate_hits CARRIES THE PLACEMENT/QUERY INVARIANT. A key is placed in the group the mapping
// names and read back from that same group; a disagreement surfaces as a missing key, not as a
// silent extra comparison. There is no separate placement accessor to compare against, because
// the index has exactly one mapping per region and both paths read it.
//
// WHAT THIS DOES NOT ESTABLISH: no speed claim, no memory claim, no generalization beyond the
// probes below, and nothing about the teammate's full BlockLearn system -- only that this
// backend, inside this index, maps, places, finds, scans and updates correctly.
//
// Output: one JSON line. Exit code 0 when gate_pass is true, 3 when it is false.

#include "index.hpp"
#include "switches.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using validation::Config;
using validation::Index;
using validation::Mode;
using validation::ModelBackend;
using validation::Pair;
using validation::Result;

using Clock = std::chrono::steady_clock;

double ms_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string jstr(const std::string& s) {
    std::string out = "\"";
    for (const char ch : s) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') { out.push_back('\\'); out.push_back(ch); }
        else if (u < 0x20) out += "?";
        else out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::string blockrank_status_name(blockrank::Status s) { return std::string(blockrank::to_string(s)); }
// ---------------------------------------------------------------- dataset

struct Corpus {
    std::vector<std::string> keys;
    std::uint64_t lines = 0, duplicates = 0, empty_keys = 0, nul_keys = 0, high_bit_keys = 0;
    std::uint64_t total_bytes = 0, dropped_by_limit = 0;
    std::size_t max_key_bytes = 0;
};

// The evaluator's raw-lines reader, mirrored exactly
// (learned-index/scripts/benchmark_lib/readers.py plus dataset.py): one key per line, trailing
// "\n" removed and then a trailing "\r" removed; unsigned-byte lexicographic sort and exact
// deduplication. A final newline does NOT produce an empty key.
Corpus load_corpus(const std::string& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open dataset: " + path);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw std::runtime_error("dataset read failed: " + path);

    Corpus c;
    std::vector<std::string> raw;
    std::size_t begin = 0;
    while (begin < data.size()) {
        const std::size_t nl = data.find('\n', begin);
        const std::size_t end = nl == std::string::npos ? data.size() : nl;
        std::size_t stop = end;
        if (stop > begin && data[stop - 1] == '\r') --stop;
        raw.emplace_back(data.substr(begin, stop - begin));
        if (nl == std::string::npos) break;
        begin = nl + 1;
    }
    data.clear();
    data.shrink_to_fit();
    c.lines = raw.size();

    std::sort(raw.begin(), raw.end());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i && raw[i] == raw[i - 1]) { ++c.duplicates; continue; }
        c.keys.push_back(std::move(raw[i]));
    }
    raw.clear();
    raw.shrink_to_fit();

    if (limit && c.keys.size() > limit) {
        c.dropped_by_limit = c.keys.size() - limit;
        c.keys.resize(limit);
    }
    for (const std::string& k : c.keys) {
        c.total_bytes += k.size();
        c.max_key_bytes = std::max(c.max_key_bytes, k.size());
        if (k.empty()) ++c.empty_keys;
        if (k.find('\0') != std::string::npos) ++c.nul_keys;
        for (const char ch : k)
            if (static_cast<unsigned char>(ch) >= 0x80) { ++c.high_bit_keys; break; }
    }
    return c;
}

std::vector<Pair> as_pairs(const std::vector<std::string>& keys) {
    std::vector<Pair> pairs;
    pairs.reserve(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i)
        pairs.emplace_back(keys[i], std::uint64_t(i));
    return pairs;
}

// The shipped query mode, as adapters/our_index/our_index_adapter.cpp builds it: interval
// routing, no prefetch, fused fingerprint, SIMD tags, hybrid overflow search at 128.
Mode query_mode() {
    Mode m;
    m.route = validation::Route::Interval;
    m.prefetch = validation::Prefetch::Off;
    m.fingerprint = true;
    m.simd = true;
    m.ov = validation::OvSearch::Hybrid;
    m.ov_thr = 128;
    m.locate = false;
    return m;
}

Config entry_config(bool blockrank) {
    Config cfg = e25sw::baseline_config(128);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = blockrank ? ModelBackend::BlockLinearRank : ModelBackend::Context16;
    return cfg;
}

// ---------------------------------------------------------------- counters

struct ProbeTotals {
    std::uint64_t queries = 0, found = 0;
    std::uint64_t model_steps = 0, candidate_blocks = 0, subblocks_visited = 0;
    std::uint64_t simd_batches = 0, tag_candidates = 0, str_cmps_primary = 0;
    std::uint64_t str_cmps_overflow = 0, overflow_entered = 0, overflow_checks = 0;
    void add(const Result& r) {
        ++queries;
        found += r.found ? 1 : 0;
        model_steps += r.steps;
        candidate_blocks += r.candidate_blocks;
        subblocks_visited += r.subblocks_visited;
        simd_batches += r.simd_batches;
        tag_candidates += r.tag_candidates;
        str_cmps_primary += r.str_cmps_primary;
        str_cmps_overflow += r.str_cmps_overflow;
        overflow_entered += r.overflow_entered;
        overflow_checks += r.overflow_checks;
    }
};

std::string totals_json(const ProbeTotals& t) {
    std::ostringstream o;
    o << "{\"queries\":" << t.queries << ",\"found\":" << t.found
      << ",\"model_steps\":" << t.model_steps
      << ",\"candidate_blocks\":" << t.candidate_blocks
      << ",\"subblocks_visited\":" << t.subblocks_visited
      << ",\"simd_batches\":" << t.simd_batches
      << ",\"tag_candidates\":" << t.tag_candidates
      << ",\"primary_comparisons\":" << t.str_cmps_primary
      << ",\"overflow_comparisons\":" << t.str_cmps_overflow
      << ",\"overflow_entered\":" << t.overflow_entered
      << ",\"overflow_checks\":" << t.overflow_checks << "}";
    return o.str();
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    std::size_t idx = static_cast<std::size_t>(std::ceil(p / 100.0 * values.size()));
    if (idx == 0) idx = 1;
    if (idx > values.size()) idx = values.size();
    return values[idx - 1];
}
// ---------------------------------------------------------------- rank gates

struct RankCheck {
    std::uint64_t regions = 0;
    std::uint64_t keys = 0;
    std::uint64_t models_fitted = 0;
    std::uint64_t monotone_violations = 0;
    std::uint64_t range_violations = 0;
    std::uint64_t recompute_mismatch = 0;
    std::uint64_t prefix_violations = 0;
    std::uint64_t replay_mismatch = 0;
    std::uint64_t replay_regions = 0;
    std::uint64_t fit_ns = 0;
    std::map<std::string, std::uint64_t> status_counts;
};

void count_status(RankCheck& out, const std::string& s) { ++out.status_counts[s]; }

// Splits the sorted keys by locate() and checks, for every region, the properties the layout
// rests on. `independent_fit` is false for the Context16 control: there the mapping is the
// reference table and the checker has nothing independent to fit for it.
RankCheck check_rank(const Index& index, const std::vector<std::string>& keys, bool independent_fit) {
    RankCheck out;
    if (keys.empty()) return out;

    std::vector<std::size_t> region_of(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) region_of[i] = index.locate(keys[i]);

    std::size_t first = 0;
    while (first < keys.size()) {
        const std::size_t rid = region_of[first];
        std::size_t last = first + 1;
        while (last < keys.size() && region_of[last] == rid) ++last;

        const auto& r = index.region(rid);
        ++out.regions;

        // The checker derives the region prefix ITSELF from this region's own key range.
        std::string prefix = keys[first];
        {
            const std::string& b = keys[last - 1];
            std::size_t p = 0;
            while (p < prefix.size() && p < b.size() && prefix[p] == b[p]) ++p;
            prefix.resize(p);
        }
        if (!r.accepts_prefix(prefix)) ++out.prefix_violations;
        if (!prefix.empty() &&
            r.accepts_prefix(std::string_view(prefix).substr(0, prefix.size() - 1)))
            ++out.prefix_violations;

        std::vector<std::string_view> view;
        view.reserve(last - first);
        for (std::size_t i = first; i < last; ++i) view.push_back(keys[i]);

        blockrank::RegionModel model;
        if (independent_fit) {
            const auto t0 = Clock::now();
            model = blockrank::RegionModel::fit(view.data(), view.size(), prefix.size(), r.groups());
            out.fit_ns += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
            ++out.models_fitted;
        }

        std::uint32_t previous = 0;
        for (std::size_t i = first; i < last; ++i) {
            const std::uint32_t actual = r.placement_group(keys[i]);
            if (actual >= r.groups()) ++out.range_violations;
            if (i > first && actual < previous) ++out.monotone_violations;
            if (independent_fit && model.predict_group(keys[i]) != actual) ++out.recompute_mismatch;
            previous = actual;
        }
        out.keys += (last - first);
        first = last;
    }
    return out;
}

// The demand replay runs on a SINGLE-BLOCK copy of the same config (E16: that is the shape the
// replay implements), and its ids are required to equal the placement mapping. This is the E38
// property -- the capacity plan and the build must read one mapping -- checked from outside.
std::uint64_t check_stats_replay(const Index& index, const std::vector<Pair>& pairs,
                                 const Config& cfg) {
    Config stats_cfg = cfg;
    stats_cfg.multiblock = false;
    Index stats(stats_cfg);
    const auto ctx = stats.demand_profile_ctx(pairs);
    if (ctx.regions.size() != index.region_count()) return 1;
    std::uint64_t mismatch = 0;
    const std::size_t chunk = cfg.region_keys ? cfg.region_keys : pairs.size();
    for (std::size_t r = 0; r < ctx.regions.size(); ++r) {
        const auto& rd = ctx.regions[r];
        if (rd.groups != index.region(r).groups()) ++mismatch;
        for (std::size_t i = 0; i < rd.group_ids.size(); ++i) {
            const std::size_t key_index = r * chunk + i;
            if (key_index >= pairs.size()) { ++mismatch; continue; }
            if (rd.group_ids[i] != index.region(r).placement_group(pairs[key_index].first))
                ++mismatch;
        }
    }
    return mismatch;
}

// ---------------------------------------------------------------- probes

struct MissProbe {
    std::uint64_t probes = 0, skipped_present = 0, mismatches = 0;
};

// Probes are only asserted absent when the INDEPENDENT reference says they are absent; a
// candidate that happens to be a real key is skipped and counted, never silently accepted.
MissProbe probe_absent(const Index& index, const Mode& mode, const std::vector<std::string>& keys,
                       const std::map<std::string, std::uint64_t>& ref, std::size_t stride) {
    MissProbe out;
    std::vector<std::string> cands;
    cands.reserve(3);
    for (std::size_t i = 0; i < keys.size(); i += stride) {
        const std::string& k = keys[i];
        cands.clear();
        cands.push_back(k + '\x01');
        cands.push_back(k + '\x00');
        if (!k.empty()) {
            std::string f = k;
            f[f.size() - 1] = static_cast<char>(static_cast<unsigned char>(f[f.size() - 1]) ^ 0x01);
            cands.push_back(f);
        }
        for (const std::string& cand : cands) {
            if (ref.find(cand) != ref.end()) { ++out.skipped_present; continue; }
            ++out.probes;
            const Result r = index.find(cand, mode);
            if (r.found) ++out.mismatches;
        }
    }
    return out;
}

// The negative control: the comparisons above must be able to FAIL. Each case feeds a
// deliberately wrong input through the same predicate the gates use.
struct NegativeControl {
    std::uint64_t cases = 0, detected = 0;
};

NegativeControl negative_control() {
    NegativeControl nc;

    // (a) order predicate vs a mapping that wraps around: (i % groups) drops at every wrap.
    {
        ++nc.cases;
        std::vector<std::uint32_t> g;
        for (std::uint32_t i = 0; i < 100; ++i) g.push_back(i % 4);
        std::uint64_t violations = 0;
        for (std::size_t i = 1; i < g.size(); ++i)
            if (g[i] < g[i - 1]) ++violations;
        if (violations > 0) ++nc.detected;
    }
    // (b) the recomputation comparison vs a shifted answer for every third key.
    {
        ++nc.cases;
        std::uint64_t mismatch = 0;
        for (std::uint32_t i = 0; i < 100; ++i) {
            const std::uint32_t expected = i % 7;
            const std::uint32_t actual = (i % 3 == 0) ? (expected + 1) % 7 : expected;
            if (actual != expected) ++mismatch;
        }
        if (mismatch > 0) ++nc.detected;
    }
    // (c) the hit comparison vs a wrong value.
    {
        ++nc.cases;
        std::uint64_t mismatch = 0;
        for (std::uint64_t i = 0; i < 100; ++i) {
            const std::uint64_t expected = i;
            const std::uint64_t actual = (i == 50) ? i + 1 : i;
            if (actual != expected) ++mismatch;
        }
        if (mismatch > 0) ++nc.detected;
    }
    return nc;
}

}  // namespace
int main(int argc, char** argv) {
    std::string corpus_name, keys_path, backend;
    std::size_t limit = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--limit") {
            if (i + 1 >= argc) { std::cerr << "--limit needs a value\n"; return 2; }
            limit = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (a.rfind("--limit=", 0) == 0) {
            limit = static_cast<std::size_t>(std::stoull(a.substr(8)));
        } else if (a.rfind("--corpus=", 0) == 0) {
            corpus_name = a.substr(9);
        } else if (keys_path.empty()) {
            keys_path = a;
        } else if (backend.empty()) {
            backend = a;
        } else {
            std::cerr << "unexpected argument " << a << "\n";
            return 2;
        }
    }
    if (keys_path.empty() || backend.empty()) {
        std::cerr << "usage: check_block_model_backend <keys-file> <context16|blockrank> "
                     "[--corpus=NAME] [--limit=N]\n";
        return 2;
    }
    const bool blockrank = backend == "blockrank";
    if (!blockrank && backend != "context16") {
        std::cerr << "backend must be context16 or blockrank\n";
        return 2;
    }

    try {
        const Corpus corpus = load_corpus(keys_path, limit);
        const std::vector<Pair> pairs = as_pairs(corpus.keys);
        std::map<std::string, std::uint64_t> ref;
        for (const Pair& p : pairs) ref.emplace(p.first, p.second);
        const Mode mode = query_mode();
        const Config cfg = entry_config(blockrank);

        validation::Region::reset_block_rank_stats();
        validation::Region::reset_lits_stats();

        const auto build_t0 = Clock::now();
        Index index(cfg);
        index.bulk_load(pairs);
        const double build_ms = ms_since(build_t0);

        const bool gate_load = index.size() == pairs.size();
        const std::size_t regions_static = index.region_count();
        const std::uint64_t rows_static = index.size();
        // Overflow is read BEFORE the mutating phases: rebuild_all() eliminates it by
        // construction, so a post-rebuild count would answer a different question.
        const std::uint64_t overflow_static = index.overflow_count();

        // The backend's own counters are read ONCE, here: every later phase (the demand replay,
        // the domain index, the boundary indexes, the split trigger) builds more regions and
        // would inflate a counter that is meant to describe the structure just measured.
        const auto coverage = validation::Region::block_rank_stats();
        bool gate_coverage = true;
        if (blockrank) {
            if (coverage.fits != index.region_count()) gate_coverage = false;
            if (coverage.regions_fitted + coverage.regions_boundary != index.region_count())
                gate_coverage = false;
            if (coverage.keys_fitted + coverage.keys_boundary != pairs.size()) gate_coverage = false;
            if (coverage.regions_fitted && coverage.segments_total == 0) gate_coverage = false;
            if (coverage.segments_total > 8 * coverage.regions_fitted) gate_coverage = false;
        }

        // ---- hits, and the access counters that come with them
        ProbeTotals hit_totals;
        std::uint64_t hit_mismatch = 0;
        for (const Pair& p : pairs) {
            const Result r = index.find(p.first, mode);
            hit_totals.add(r);
            if (!r.found || r.value != p.second) ++hit_mismatch;
        }
        const bool gate_hits = hit_mismatch == 0;

        // ---- absent probes
        const std::size_t stride = std::max<std::size_t>(1, corpus.keys.size() / 2048);
        const MissProbe miss = probe_absent(index, mode, corpus.keys, ref, stride);
        const bool gate_miss = miss.mismatches == 0;

        // ---- region partition
        std::uint64_t partition_violations = 0;
        {
            std::size_t previous = 0;
            for (std::size_t i = 0; i < corpus.keys.size(); ++i) {
                const std::size_t rid = index.locate(corpus.keys[i]);
                if (i && rid < previous) ++partition_violations;
                previous = rid;
            }
        }
        const bool gate_partition = partition_violations == 0;

        // ---- the rank-specific checks
        RankCheck rank = check_rank(index, corpus.keys, blockrank);
        for (std::size_t r = 0; r < index.region_count(); ++r)
            count_status(rank, blockrank
                                   ? blockrank_status_name(index.region(r).block_rank_status())
                                   : std::string("not_applicable"));
        rank.replay_mismatch = check_stats_replay(index, pairs, cfg);

        // ---- structural readout: the single-block demand replay's own group statistics
        std::vector<std::uint64_t> group_entries, group_bytes;
        std::uint64_t groups_total = 0, groups_empty = 0, subblocks_total = 0;
        for (std::size_t r = 0; r < index.region_count(); ++r) {
            const auto& reg = index.region(r);
            groups_total += reg.groups();
            for (std::uint32_t g = 0; g < reg.groups(); ++g) subblocks_total += reg.subblock_count(g);
        }
        rank.replay_regions = index.region_count();
        {
            Config stats_cfg = cfg;
            stats_cfg.multiblock = false;
            Index stats(stats_cfg);
            const auto ctx = stats.demand_profile_ctx(pairs);
            rank.replay_regions = ctx.regions.size();
            for (const auto& rd : ctx.regions) {
                for (const auto& gs : rd.stats) {
                    const std::uint64_t entries = gs.primary_entries + gs.overflow_entries;
                    group_entries.push_back(entries);
                    group_bytes.push_back(gs.primary_bytes + gs.overflow_bytes);
                    if (entries == 0) ++groups_empty;
                }
            }
        }

        // ---- domain: empty key, embedded NUL, high bytes, shared prefixes, a 4000-byte key
        bool gate_domain = true;
        std::uint64_t domain_keys = 0, domain_hit_mismatch = 0, domain_monotone = 0,
                      domain_recompute = 0;
        MissProbe domain_miss;
        std::string domain_status = "n/a";
        {
            std::vector<std::string> dom;
            dom.push_back(std::string());
            dom.push_back(std::string(1, '\0'));
            dom.push_back(std::string("\0\0\x01", 3));
            for (int i = 0; i < 512; ++i) {
                std::string k;
                k.push_back(static_cast<char>(0x80 + (i % 128)));
                k.append(8, static_cast<char>(i & 0xff));
                k += "high";
                dom.push_back(k);
            }
            for (int i = 0; i < 512; ++i) {
                std::string k = "SHAREDPREFIX________";
                k.push_back(static_cast<char>(i & 0xff));
                k.push_back(static_cast<char>((i >> 8) & 0xff));
                k += "tail";
                dom.push_back(k);
            }
            dom.push_back(std::string(4000, 'Z'));
            dom.push_back(std::string(4000, 'Z') + '\x01');
            std::sort(dom.begin(), dom.end());
            dom.erase(std::unique(dom.begin(), dom.end()), dom.end());

            Config dcfg = cfg;
            dcfg.region_keys = 512;
            std::map<std::string, std::uint64_t> dref;
            std::unique_ptr<Index> dindex;
            {
                // The training input dies here, before anything is asked of the index. The
                // teammate's BlockModel keeps a `string_view` INTO its training keys; this
                // port keeps a number instead, and this scope is what holds that claim to
                // evidence rather than to a comment. Anything dangling would show up as a
                // wrong answer here (and under ASan in the sanitizer cells).
                std::vector<Pair> dpairs = as_pairs(dom);
                for (const Pair& p : dpairs) dref.emplace(p.first, p.second);
                dindex = std::make_unique<Index>(dcfg);
                dindex->bulk_load(dpairs);
            }

            domain_keys = dom.size();
            for (const auto& kv : dref) {
                const Result r = dindex->find(kv.first, mode);
                if (!r.found || r.value != kv.second) ++domain_hit_mismatch;
            }
            domain_miss = probe_absent(*dindex, mode, dom, dref, 1);
            const RankCheck dcheck = check_rank(*dindex, dom, blockrank);
            domain_monotone = dcheck.monotone_violations + dcheck.range_violations +
                              dcheck.prefix_violations;
            domain_recompute = dcheck.recompute_mismatch;
            if (blockrank && dindex->region_count())
                domain_status = blockrank_status_name(dindex->region(0).block_rank_status());
            gate_domain = domain_hit_mismatch == 0 && domain_miss.mismatches == 0 &&
                          domain_monotone == 0 && domain_recompute == 0;
        }
        // ---- boundary cases: the three statuses the model DEFINES, plus a fixed-G pair
        bool gate_boundary = true;
        std::string b_empty_status = "n/a", b_one_status = "n/a", b_one_group_status = "n/a",
                    b_eight_status = "n/a";
        {
            std::vector<Pair> none;
            Index empty_index(cfg);
            empty_index.bulk_load(none);
            const Result r = empty_index.find("anything", mode);
            if (blockrank)
                b_empty_status = blockrank_status_name(empty_index.region(0).block_rank_status());
            gate_boundary = gate_boundary && empty_index.size() == 0 && !r.found &&
                            empty_index.region_count() == 1;

            std::vector<Pair> one;
            one.emplace_back("only-key", 7);
            Index one_index(cfg);
            one_index.bulk_load(one);
            const Result r1 = one_index.find("only-key", mode);
            if (blockrank) b_one_status = blockrank_status_name(one_index.region(0).block_rank_status());
            gate_boundary = gate_boundary && one_index.size() == 1 && r1.found && r1.value == 7;
            const Result r2 = one_index.find("only-kez", mode);
            gate_boundary = gate_boundary && !r2.found;

            std::vector<std::string> many(corpus.keys.begin(),
                                          corpus.keys.begin() +
                                              std::min<std::size_t>(corpus.keys.size(), 1024));
            const std::vector<Pair> many_pairs = as_pairs(many);

            Config one_group = cfg;
            one_group.fixed_groups = 1;
            Index g1(one_group);
            g1.bulk_load(many_pairs);
            if (blockrank) b_one_group_status = blockrank_status_name(g1.region(0).block_rank_status());
            std::uint64_t one_group_bad = 0;
            for (const Pair& p : many_pairs) {
                const Result r = g1.find(p.first, mode);
                if (!r.found || r.value != p.second) ++one_group_bad;
                if (blockrank && g1.region(0).placement_group(p.first) != 0) ++one_group_bad;
            }
            gate_boundary = gate_boundary && one_group_bad == 0;

            Config eight = cfg;
            eight.fixed_groups = 8;
            Index g8(eight);
            g8.bulk_load(many_pairs);
            if (blockrank) b_eight_status = blockrank_status_name(g8.region(0).block_rank_status());
            std::uint64_t eight_bad = 0;
            for (const Pair& p : many_pairs) {
                const Result r = g8.find(p.first, mode);
                if (!r.found || r.value != p.second) ++eight_bad;
            }
            const RankCheck g8check = check_rank(g8, many, blockrank);
            eight_bad += g8check.monotone_violations + g8check.recompute_mismatch;
            gate_boundary = gate_boundary && eight_bad == 0;
            if (blockrank) {
                gate_boundary = gate_boundary && b_empty_status == "no_keys" &&
                                b_one_status == "too_few_keys" &&
                                b_one_group_status == "single_group" && b_eight_status == "ok";
            }
        }

        // ---- dynamic program: inserts, erases, updates, a positive split trigger
        bool gate_dynamic = true;
        std::uint64_t dynamic_mismatch = 0, dynamic_splits = 0, dynamic_rebuilds = 0,
                      dynamic_monotone = 0, dynamic_live = 0,
                      dynamic_scan_rows = 0, op_rejections = 0;
        std::uint64_t post_rebuild_splits = 0, post_rebuild_rebuilds = 0,
                      post_rebuild_monotone = 0, post_rebuild_recompute = 0,
                      post_rebuild_hit_mismatch = 0;
        {
            const std::size_t base = corpus.keys.size();
            const std::uint64_t splits0 = index.splits, rebuilds0 = index.rebuilds;

            auto checkpoint = [&]() {
                if (index.size() != ref.size()) ++dynamic_mismatch;
                if (ref.empty()) return;
                const std::string& lo = ref.begin()->first;
                const auto rows = index.scan(lo, index.size());
                if (rows.size() != ref.size()) { ++dynamic_mismatch; return; }
                ++dynamic_scan_rows;
                auto it = ref.begin();
                for (const Pair& row : rows) {
                    if (it == ref.end() || row.first != it->first || row.second != it->second) {
                        ++dynamic_mismatch;
                        return;
                    }
                    ++it;
                }
            };

            if (base) {
                // Phase 1: inserts derived from corpus keys -- the shape the M3 lifecycle gate
                // uses, where the appended suffix keeps the region's prefix intact.
                for (int i = 0; i < 800; ++i) {
                    const std::string k = corpus.keys[(std::size_t(i) * 2654435u) % base] +
                                          "#bm" + std::to_string(i);
                    if (ref.find(k) != ref.end()) { ++op_rejections; continue; }
                    const std::uint64_t v = 0x41000000ull + std::uint64_t(i);
                    if (!index.insert(k, v)) ++op_rejections;
                    ref.emplace(k, v);
                }
                checkpoint();
                // Phase 2: erases of corpus keys, never the last one (the split stem below must
                // stay above every remaining key without having to exist).
                std::size_t erased = 0;
                const std::size_t erase_target = std::min<std::size_t>(300, base / 2);
                for (std::size_t p = 0; p < base && erased < erase_target; ++p) {
                    const std::size_t pick = (p * 7919u) % base;
                    if (pick + 1 == base) continue;   // never the split stem itself
                    const std::string& k = corpus.keys[pick];
                    auto it = ref.find(k);
                    if (it == ref.end()) continue;
                    if (!index.erase(k)) ++op_rejections;
                    ref.erase(it);
                    ++erased;
                }
                if (erased != erase_target) ++dynamic_mismatch;
                checkpoint();
                // Phase 3: overwrite-updates of surviving keys.
                std::size_t seen = 0, updates_done = 0;
                for (auto it = ref.begin(); it != ref.end() && updates_done < 300; ++it) {
                    if ((seen++ % 7) != 3) continue;
                    const std::uint64_t v = it->second * 2 + 1;
                    if (!index.update(it->first, v)) ++op_rejections;
                    it->second = v;
                    ++updates_done;
                }
                if (updates_done != 300) ++dynamic_mismatch;
                checkpoint();
                // Phase 4: the split trigger. Ascending keys above every corpus key land in the
                // last region; 4200 of them push it past 2 * region_keys whichever remainder it
                // started with, so Index::insert must rebuild and Index::splits must grow.
                const std::string stem = corpus.keys.back();
                for (std::size_t i = 0; i < 4200; ++i) {
                    const std::string k = stem + "-bmsplit-" + std::to_string(1000000 + i);
                    if (ref.find(k) != ref.end()) { ++op_rejections; continue; }
                    const std::uint64_t v = 0x42000000ull + i;
                    if (!index.insert(k, v)) ++op_rejections;
                    ref.emplace(k, v);
                }
                checkpoint();
                dynamic_splits = index.splits - splits0;
                dynamic_rebuilds = index.rebuilds - rebuilds0;
                gate_dynamic = dynamic_mismatch == 0 && op_rejections == 0 && dynamic_splits > 0;

                std::vector<std::string> live_keys;
                live_keys.reserve(ref.size());
                for (const auto& kv : ref) live_keys.push_back(kv.first);
                // ORDER ONLY here: after a split the surviving regions keep the model fitted at
                // their last construction, so their mapping is a snapshot of an earlier key set
                // and re-fitting it in the checker would compare two different epochs, not two
                // implementations. The recomputation gate runs after rebuild_all() below.
                const RankCheck post = check_rank(index, live_keys, false);
                dynamic_monotone = post.monotone_violations + post.range_violations +
                                   post.prefix_violations;
                dynamic_live = live_keys.size();
                gate_dynamic = gate_dynamic && dynamic_monotone == 0;

                // Every region refits from its CURRENT data, so from here the checker's own fit
                // is required to answer exactly like the region's for every live key -- and the
                // rebuilt index must still find every key.
                const std::uint64_t rebuilds_before = index.rebuilds;
                index.rebuild_all();
                post_rebuild_splits = index.splits - splits0 - dynamic_splits;
                post_rebuild_rebuilds = index.rebuilds - rebuilds_before;
                std::uint64_t post_hit_mismatch = 0;
                for (const auto& kv : ref) {
                    const Result r = index.find(kv.first, mode);
                    if (!r.found || r.value != kv.second) ++post_hit_mismatch;
                }
                const RankCheck rebuilt = check_rank(index, live_keys, blockrank);
                post_rebuild_monotone = rebuilt.monotone_violations + rebuilt.range_violations +
                                        rebuilt.prefix_violations;
                post_rebuild_recompute = rebuilt.recompute_mismatch;
                post_rebuild_hit_mismatch = post_hit_mismatch;
                gate_dynamic = gate_dynamic && post_hit_mismatch == 0 &&
                               post_rebuild_monotone == 0 &&
                               (!blockrank || post_rebuild_recompute == 0) &&
                               post_rebuild_rebuilds >= regions_static;
            }
        }

        const NegativeControl nc = negative_control();
        const bool gate_negative_control = nc.detected == nc.cases && nc.cases >= 3;

        const bool gate_rank_prefix = !blockrank || rank.prefix_violations == 0;
        const bool gate_rank_monotone = !blockrank || rank.monotone_violations == 0;
        const bool gate_rank_range = !blockrank || rank.range_violations == 0;
        const bool gate_rank_recompute = !blockrank || rank.recompute_mismatch == 0;
        const bool gate_stats_replay = !blockrank || rank.replay_mismatch == 0;

        const bool gate_pass = gate_load && gate_hits && gate_miss && gate_partition &&
                               gate_domain && gate_boundary && gate_dynamic &&
                               gate_negative_control && gate_rank_prefix && gate_rank_monotone &&
                               gate_rank_range && gate_rank_recompute && gate_stats_replay &&
                               gate_coverage;

        std::ostringstream o;
        o << "{";
        o << "\"phase\":\"block_model_entry\"";
        o << ",\"corpus\":" << jstr(corpus_name);
        o << ",\"backend\":" << jstr(backend);
        o << ",\"rank_applicable\":" << (blockrank ? "true" : "false");
        o << ",\"official_timings\":false";
        o << ",\"limit\":" << limit;
        o << ",\"rows\":" << pairs.size();
        o << ",\"source_lines\":" << corpus.lines;
        o << ",\"duplicates\":" << corpus.duplicates;
        o << ",\"dropped_by_limit\":" << corpus.dropped_by_limit;
        o << ",\"empty_keys\":" << corpus.empty_keys;
        o << ",\"nul_keys\":" << corpus.nul_keys;
        o << ",\"high_bit_keys\":" << corpus.high_bit_keys;
        o << ",\"max_key_bytes\":" << corpus.max_key_bytes;
        o << ",\"total_key_bytes\":" << corpus.total_bytes;
        o << ",\"regions_static\":" << regions_static;
        o << ",\"rows_static\":" << rows_static;
        o << ",\"regions_final\":" << index.region_count();
        o << ",\"groups_total\":" << groups_total;
        o << ",\"groups_empty\":" << groups_empty;
        o << ",\"subblocks_total\":" << subblocks_total;
        o << ",\"overflow_records_static\":" << overflow_static;
        o << ",\"overflow_records_final\":" << index.overflow_count();
        o << ",\"group_entries\":{\"p50\":" << percentile(group_entries, 50)
          << ",\"p95\":" << percentile(group_entries, 95)
          << ",\"p99\":" << percentile(group_entries, 99)
          << ",\"max\":"
          << (group_entries.empty() ? 0 : *std::max_element(group_entries.begin(), group_entries.end()))
          << "}";
        o << ",\"group_bytes\":{\"p50\":" << percentile(group_bytes, 50)
          << ",\"p95\":" << percentile(group_bytes, 95)
          << ",\"p99\":" << percentile(group_bytes, 99)
          << ",\"max\":"
          << (group_bytes.empty() ? 0 : *std::max_element(group_bytes.begin(), group_bytes.end()))
          << "}";
        o << ",\"build_ms\":" << build_ms;
        o << ",\"fit_total_ms\":" << (double(rank.fit_ns) / 1e6);
        o << ",\"hits\":" << hit_totals.found;
        o << ",\"hit_mismatch\":" << hit_mismatch;
        o << ",\"miss_probes\":" << miss.probes;
        o << ",\"miss_mismatch\":" << miss.mismatches;
        o << ",\"miss_skipped_present\":" << miss.skipped_present;
        o << ",\"hit_probe_totals\":" << totals_json(hit_totals);
        o << ",\"partition_violations\":" << partition_violations;
        o << ",\"monotone_violations\":" << rank.monotone_violations;
        o << ",\"range_violations\":" << rank.range_violations;
        o << ",\"recompute_mismatch\":" << rank.recompute_mismatch;
        o << ",\"prefix_violations\":" << rank.prefix_violations;
        o << ",\"models_fitted\":" << rank.models_fitted;
        o << ",\"keys_checked\":" << rank.keys;
        o << ",\"status_counts\":{";
        {
            bool first_status = true;
            for (const auto& kv : rank.status_counts) {
                o << (first_status ? "" : ",") << jstr(kv.first) << ":" << kv.second;
                first_status = false;
            }
        }
        o << "}";
        o << ",\"stats_replay_mismatch\":" << rank.replay_mismatch;
        o << ",\"stats_replay_regions\":" << rank.replay_regions;
        o << ",\"coverage_regions_fitted\":" << coverage.regions_fitted;
        o << ",\"coverage_keys_fitted\":" << coverage.keys_fitted;
        o << ",\"coverage_regions_boundary\":" << coverage.regions_boundary;
        o << ",\"coverage_keys_boundary\":" << coverage.keys_boundary;
        o << ",\"coverage_segments_total\":" << coverage.segments_total;
        o << ",\"coverage_fits\":" << coverage.fits;
        o << ",\"domain_keys\":" << domain_keys;
        o << ",\"domain_hit_mismatch\":" << domain_hit_mismatch;
        o << ",\"domain_miss_probes\":" << domain_miss.probes;
        o << ",\"domain_miss_mismatch\":" << domain_miss.mismatches;
        o << ",\"domain_rank_violations\":" << (domain_monotone + domain_recompute);
        o << ",\"domain_status\":" << jstr(domain_status);
        o << ",\"boundary_empty_status\":" << jstr(b_empty_status);
        o << ",\"boundary_one_key_status\":" << jstr(b_one_status);
        o << ",\"boundary_one_group_status\":" << jstr(b_one_group_status);
        o << ",\"boundary_eight_group_status\":" << jstr(b_eight_status);
        o << ",\"dynamic_live\":" << dynamic_live;
        o << ",\"dynamic_scan_checkpoints\":" << dynamic_scan_rows;
        o << ",\"dynamic_mismatch\":" << dynamic_mismatch;
        o << ",\"dynamic_op_rejections\":" << op_rejections;
        o << ",\"dynamic_splits\":" << dynamic_splits;
        o << ",\"dynamic_rebuilds\":" << dynamic_rebuilds;
        o << ",\"dynamic_rank_violations\":" << dynamic_monotone;
        o << ",\"post_rebuild_splits\":" << post_rebuild_splits;
        o << ",\"post_rebuild_rebuilds\":" << post_rebuild_rebuilds;
        o << ",\"post_rebuild_hit_mismatch\":" << post_rebuild_hit_mismatch;
        o << ",\"post_rebuild_rank_violations\":"
          << (post_rebuild_monotone + post_rebuild_recompute);
        o << ",\"negative_control_cases\":" << nc.cases;
        o << ",\"negative_control_detected\":" << nc.detected;
        {
            auto gate = [&o](const char* name, bool value, bool applicable) {
                o << "," << jstr(name) << ":";
                if (!applicable) o << "null";
                else o << (value ? "true" : "false");
            };
            gate("gate_load", gate_load, true);
            gate("gate_hits", gate_hits, true);
            gate("gate_miss", gate_miss, true);
            gate("gate_partition", gate_partition, true);
            gate("gate_domain", gate_domain, true);
            gate("gate_boundary", gate_boundary, true);
            gate("gate_dynamic", gate_dynamic, true);
            gate("gate_negative_control", gate_negative_control, true);
            gate("gate_rank_prefix", gate_rank_prefix, blockrank);
            gate("gate_rank_monotone", gate_rank_monotone, blockrank);
            gate("gate_rank_range", gate_rank_range, blockrank);
            gate("gate_rank_recompute", gate_rank_recompute, blockrank);
            gate("gate_stats_replay", gate_stats_replay, blockrank);
            gate("gate_coverage", gate_coverage, blockrank);
        }
        o << ",\"gate_pass\":" << (gate_pass ? "true" : "false");
        o << "}";
        std::cout << o.str() << std::endl;
        return gate_pass ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "check_block_model_backend failed: " << e.what() << "\n";
        return 4;
    }
}
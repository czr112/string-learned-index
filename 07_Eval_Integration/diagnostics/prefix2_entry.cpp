// PFX2 entry diagnosis: six counted cells (two backends x three corpora). Counters only.
//
// Candidate under test: docs/NEXT_ACTION_PLAN_20261011_MODEL.md section 3 -- a two-level
// prefix table per Context16 region. Level 1 holds 256 entries (determined group / level-2
// row / fallback); level 2 holds one 256 x 4-byte row per first byte whose group the model
// has not fixed after one step.
//
// Where every number comes from (stated so the report can cite it):
//   * stop step of the production path (Route::Interval) on extended probe keys. The probe
//     suffix is at least five bytes long, so `steps < suffix length` can only mean the
//     early-stop condition fired: the string did not end there.
//   * cached group: the model full prediction (Region::diag_group, Route::Full, which reads
//     every byte) on the same probe, required to agree with a second probe using a DIFFERENT
//     extension. bucket() is monotone over a shrinking interval, so the full-route group is
//     the Interval-route group; the replay pass exercises that equality on real keys.
//   * replay: for every corpus key and every query, the table-served (group, consumed) and
//     its CRC state and tag are compared against the production path, computed here without
//     consulting the table.
//   * space: planned flat level-1 array + per-region row index + level-2 rows, plus object
//     accounting. Capacity slack and generation peak are reported separately.
//
// Probes are keyed by (region, first byte[, second byte]) and are never derived from queries
// or from stored records; queries are not used to train or filter table entries.
// This is a counter build. No timing is taken and no count here converts to a speedup.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main

#ifndef E12_COUNT
#error PFX2 entry requires E12_COUNT (counters only, never a timing build)
#endif

#include <array>

namespace pfx2 {

using validation::Fingerprint;
using validation::Index;
using validation::Region;
using validation::Result;

constexpr std::uint32_t kNoRow = 0xffffffffu;

struct L2Row {
    std::array<std::uint32_t, 256> group{};
    std::array<std::uint8_t, 256> ok{};
};

struct Table {
    bool present = false;
    std::array<std::uint32_t, 256> l1_group{};
    std::array<std::uint8_t, 256> l1_ok{};
    std::array<std::uint32_t, 256> row_of{};
    std::vector<L2Row> rows;
};

enum class Serve : std::uint8_t { Fallback = 0, Level1 = 1, Level2 = 2 };

struct Answer {
    Serve kind;
    std::uint32_t group;
    std::uint32_t consumed;
};

// The candidate lookup, exactly as the plan fixes it: a one-byte suffix may be served by
// level 1 only (level 2 needs a second byte); empty suffixes never hit.
inline Answer serve(const Table& t, std::string_view s) {
    if (t.present && !s.empty()) {
        const auto b1 = static_cast<unsigned char>(s[0]);
        if (t.l1_ok[b1]) return {Serve::Level1, t.l1_group[b1], 1};
        if (s.size() >= 2) {
            const auto row = t.row_of[b1];
            if (row != kNoRow) {
                const auto b2 = static_cast<unsigned char>(s[1]);
                if (t.rows[row].ok[b2]) return {Serve::Level2, t.rows[row].group[b2], 2};
            }
        }
    }
    return {Serve::Fallback, 0, 0};
}

struct FingerprintView {
    std::uint32_t state = 0;
    std::uint8_t tag = 0;
};

inline FingerprintView fingerprint_of(std::string_view suffix, std::uint32_t consumed) {
    if (consumed > suffix.size()) throw std::runtime_error("consumed beyond suffix");
    Fingerprint fp;
    for (std::uint32_t i = 0; i < consumed; ++i) fp.byte(static_cast<std::uint8_t>(suffix[i]));
    fp.tail(suffix.substr(consumed));
    return {fp.state, fp.tag()};
}

inline std::uint32_t region_group(const Index& index, std::size_t r, std::string_view key) {
    const Region& reg = index.region(r);
    const std::uint32_t g = reg.diag_group(key);
    if (g == UINT32_MAX || g >= reg.groups())
        throw std::runtime_error("region group out of range");
    return g;
}

inline std::string probe_key(const std::string& base, std::string_view ext, unsigned b1, int b2 = -1) {
    std::string key;
    key.reserve(base.size() + 2 + ext.size());
    key += base;
    key.push_back(static_cast<char>(b1));
    if (b2 >= 0) key.push_back(static_cast<char>(b2));
    key.append(ext);
    return key;
}

struct Tally {
    // query replay
    std::uint64_t queries_checked = 0, rows_checked = 0, edge_checked = 0, edge_hits_l1 = 0;
    std::uint64_t hit_l1 = 0, hit_l2 = 0, fallback = 0;
    std::uint64_t fb_prefix = 0, fb_empty = 0, fb_len1 = 0, fb_row_missing = 0, fb_row_entry = 0;
    std::uint64_t hit_l1_found = 0, hit_l1_miss = 0, hit_l2_found = 0, hit_l2_miss = 0;
    std::uint64_t avoided_steps = 0, total_steps = 0, table_l1_reads = 0, table_row_reads = 0;
    // rows replay (corpus differential)
    std::uint64_t rows_l1 = 0, rows_l2 = 0, rows_fallback = 0;
    // table generation / verification
    std::uint64_t regions_enrolled = 0, l1_probes = 0, l1_determined = 0, l1_verified = 0;
    std::uint64_t l1_undetermined = 0, l2_cells = 0, l2_determined = 0, l2_verified = 0, l2_rows = 0;
    std::uint64_t scratch_max = 0;
    // step histograms: bins 0..16 plus >16
    std::array<std::uint64_t, 18> steps_all{};
    std::array<std::uint64_t, 18> steps_found{};
    std::array<std::uint64_t, 18> steps_miss{};
    static std::size_t bin(std::uint32_t s) { return s > 16 ? 17 : s; }
};

void build_region(const Index& index, std::size_t r, const std::string& rep, Table& t,
                  Tally& tally, const Mode& query_mode) {
    const Region& reg = index.region(r);
    if (reg.uses_lits()) throw std::runtime_error("PFX2 table asked for a LITS region");
    if (rep.empty() || !reg.accepts_prefix(rep))
        throw std::runtime_error("region representative key does not accept the prefix");
    const std::size_t plen = reg.diag_prefix_size();
    const std::string base = rep.substr(0, plen);
    const std::string ext_a = std::string("\x00\x00\x00\x00", 4);
    const std::string ext_b = std::string("\x7f\xff\x00\x41", 4);

    t.present = true;
    t.row_of.fill(kNoRow);
    ++tally.regions_enrolled;

    std::size_t scratch = 0;
    for (unsigned b1 = 0; b1 < 256; ++b1) {
        const std::string ka = probe_key(base, ext_a, b1);
        const Result ra = index.find_known(r, ka, query_mode);
        const std::size_t la = ka.size() - plen;
        if (ra.steps == 0 || ra.steps > la) throw std::runtime_error("probe stop step out of range");
        ++tally.l1_probes;
        if (ra.steps == 1) {
            const std::uint32_t g = region_group(index, r, ka);
            const std::string kb = probe_key(base, ext_b, b1);
            const Result rb = index.find_known(r, kb, query_mode);
            if (rb.steps != 1) throw std::runtime_error("level-1 stop step not stable under extension");
            if (region_group(index, r, kb) != g)
                throw std::runtime_error("level-1 group not stable under extension");
            t.l1_group[b1] = g;
            t.l1_ok[b1] = 1;
            ++tally.l1_determined;
            ++tally.l1_verified;
            scratch = std::max(scratch, base.size() + ka.size() + kb.size());
            continue;
        }
        ++tally.l1_undetermined;
        L2Row row;
        bool any = false;
        for (unsigned b2 = 0; b2 < 256; ++b2) {
            const std::string ka2 = probe_key(base, ext_a, b1, static_cast<int>(b2));
            const Result ra2 = index.find_known(r, ka2, query_mode);
            ++tally.l2_cells;
            if (ra2.steps == 1)
                throw std::runtime_error("second-byte probe contradicts an undetermined first byte");
            if (ra2.steps != 2) continue;
            const std::uint32_t g2 = region_group(index, r, ka2);
            const std::string kb2 = probe_key(base, ext_b, b1, static_cast<int>(b2));
            const Result rb2 = index.find_known(r, kb2, query_mode);
            if (rb2.steps != 2 || region_group(index, r, kb2) != g2)
                throw std::runtime_error("level-2 entry not stable under extension");
            row.group[b2] = g2;
            row.ok[b2] = 1;
            any = true;
            ++tally.l2_determined;
            ++tally.l2_verified;
            scratch = std::max(scratch, base.size() + ka2.size() + kb2.size());
        }
        if (any) {
            t.row_of[b1] = static_cast<std::uint32_t>(t.rows.size());
            t.rows.push_back(row);
            ++tally.l2_rows;
        }
    }

    // Edge semantics fixed by the plan, checked on real short suffixes.
    // Empty suffix: production returns a zero-step prediction; a table may never serve it.
    const Result r_empty = index.find_known(r, base, query_mode);
    if (r_empty.steps != 0) throw std::runtime_error("empty suffix must not run the model");
    if (serve(t, std::string_view()).kind != Serve::Fallback)
        throw std::runtime_error("candidate served an empty suffix");
    ++tally.edge_checked;
    for (unsigned b1 = 0; b1 < 256; ++b1) {
        const std::string k1 = probe_key(base, std::string_view(), b1);
        const Result r1 = index.find_known(r, k1, query_mode);
        if (r1.steps != 1) throw std::runtime_error("one-byte suffix stop step must be one");
        const auto s1 = reg.residual(k1);
        const Answer a1 = serve(t, s1);
        if (a1.kind == Serve::Level1) {
            const std::uint32_t g = region_group(index, r, k1);
            if (a1.group != g || a1.consumed != r1.steps)
                throw std::runtime_error("one-byte suffix served differently");
            const auto ref = fingerprint_of(s1, r1.steps);
            const auto got = fingerprint_of(s1, a1.consumed);
            if (ref.state != got.state || ref.tag != got.tag)
                throw std::runtime_error("one-byte suffix fingerprint differs");
            ++tally.edge_hits_l1;
        } else if (a1.kind != Serve::Fallback) {
            throw std::runtime_error("one-byte suffix must fall back when level 1 undetermined");
        }
        ++tally.edge_checked;
    }
    // Two-byte suffixes for every determined level-2 cell.
    for (unsigned b1 = 0; b1 < 256; ++b1) {
        const auto row = t.row_of[b1];
        if (row == kNoRow) continue;
        for (unsigned b2 = 0; b2 < 256; ++b2) {
            if (!t.rows[row].ok[b2]) continue;
            const std::string k2 = probe_key(base, std::string_view(), b1, static_cast<int>(b2));
            const Result r2 = index.find_known(r, k2, query_mode);
            const auto s2 = reg.residual(k2);
            const Answer a2 = serve(t, s2);
            if (a2.kind != Serve::Level2 || a2.consumed != r2.steps ||
                a2.group != region_group(index, r, k2))
                throw std::runtime_error("two-byte suffix served differently");
            const auto ref = fingerprint_of(s2, r2.steps);
            const auto got = fingerprint_of(s2, a2.consumed);
            if (ref.state != got.state || ref.tag != got.tag)
                throw std::runtime_error("two-byte suffix fingerprint differs");
            ++tally.edge_checked;
        }
    }
    tally.scratch_max = std::max(tally.scratch_max, static_cast<std::uint64_t>(scratch));
}

// Production-path reference for one accepted key: model group, processed length and the
// CRC state the stored tag is derived from. Computed without consulting any table.
struct Reference {
    bool prefix_ok = false;
    std::uint32_t group = 0;
    std::uint32_t consumed = 0;
    FingerprintView fp{};
};

Reference reference_of(const Index& index, std::size_t r, std::string_view key, const Result& res) {
    const Region& reg = index.region(r);
    Reference out;
    out.prefix_ok = reg.accepts_prefix(key);
    if (!out.prefix_ok) return out;
    const auto s = reg.residual(key);
    if (reg.uses_lits()) {
        out.consumed = res.steps;
        out.group = region_group(index, r, key);
        Fingerprint fp;
        fp.tail(s);   // the LITS path reports fp_consumed = 0 and hashes the whole suffix
        out.fp = {fp.state, fp.tag()};
        return out;
    }
    if (res.steps > s.size()) throw std::runtime_error("stop step beyond the suffix");
    out.consumed = res.steps;
    out.group = region_group(index, r, key);
    Fingerprint fused;
    for (std::uint32_t i = 0; i < out.consumed; ++i) fused.byte(static_cast<std::uint8_t>(s[i]));
    fused.tail(s.substr(out.consumed));
    Fingerprint whole;
    whole.tail(s);
    if (fused.state != whole.state) throw std::runtime_error("fused fingerprint differs from the stream");
    out.fp = {fused.state, fused.tag()};
    return out;
}

void emit_hist(const char* name, const std::array<std::uint64_t, 18>& h) {
    std::cout << "\"" << name << "\":[";
    for (std::size_t i = 0; i < h.size(); ++i) std::cout << (i ? "," : "") << h[i];
    std::cout << "]";
}

}  // namespace pfx2

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::invalid_argument("rows.bin queries.bin context16|lits");
        auto row_input = buffered_input(argv[1]), query_input = buffered_input(argv[2]);
        const auto rows = unified::read_rows(row_input);
        const auto queries = unified::read_queries(query_input);
        if (rows.empty() || queries.empty()) throw std::invalid_argument("empty input");
        const bool lits = std::string(argv[3]) == "lits";
        if (!lits && std::string(argv[3]) != "context16") throw std::invalid_argument("backend");

        Config cfg = e25sw::baseline_config(128);
        e25sw::pin_frozen_switches(cfg);
        cfg.model_backend = lits ? ModelBackend::LitsHpt : ModelBackend::Context16;
        cfg.compact_region_routing = true;   // C on; W/S/H stay off (asserted below)
        if (!cfg.compact_plan_commit || cfg.subblock_reclaim || cfg.reclaim_widest ||
            cfg.primary_hole_reuse || cfg.reclaim_low_activity)
            throw std::runtime_error("configuration drift");

        Index index(cfg);
        std::vector<Pair> pairs;
        pairs.reserve(rows.size());
        for (const auto& row : rows) pairs.emplace_back(row.key, row.value);
        index.bulk_load(pairs);
        std::vector<Pair>().swap(pairs);

        const Mode query_mode = mode(false);
        std::vector<std::size_t> routes(queries.size());
        std::uint64_t prefix_rejected = 0, lits_queries = 0;
        for (std::size_t i = 0; i < queries.size(); ++i) {
            routes[i] = index.locate(queries[i].key);
            const auto& reg = index.region(routes[i]);
            prefix_rejected += !reg.accepts_prefix(queries[i].key);
            lits_queries += reg.uses_lits();
        }
        const auto coverage = index.lits_coverage();

        // Region representatives come from the corpus, never from queries.
        std::vector<std::string> rep(index.region_count());
        std::vector<std::size_t> row_routes(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto r = index.locate(rows[i].key);
            row_routes[i] = r;
            const auto& reg = index.region(r);
            if (rep[r].empty() && reg.accepts_prefix(rows[i].key)) rep[r] = rows[i].key;
        }

        pfx2::Tally tally;
        std::vector<pfx2::Table> tables(index.region_count());
        if (!lits) {
            for (std::size_t r = 0; r < index.region_count(); ++r) {
                if (index.region(r).uses_lits()) throw std::runtime_error("LITS region in a Context16 build");
                if (rep[r].empty()) throw std::runtime_error("region without a corpus representative");
                pfx2::build_region(index, r, rep[r], tables[r], tally, query_mode);
            }
        }

        // Measured pass: the production query path, counters isolated to this loop.
        Aggregate by_source[3];
        std::uint64_t checksum = 0;
        std::vector<std::uint32_t> q_steps(queries.size());
#ifdef E25_DIAG
        e25::counts().reset();
#endif
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto res = index.find(queries[i].key, query_mode);
            check(res, queries[i]);
            by_source[res.source].add(res);
            q_steps[i] = res.steps;
            tally.steps_all[pfx2::Tally::bin(res.steps)] += 1;
            (res.found ? tally.steps_found : tally.steps_miss)[pfx2::Tally::bin(res.steps)] += 1;
            checksum = checksum * 131 + token(res);
        }
#ifdef E25_DIAG
        const auto counted = e25::counts();
#endif

        // Query replay: production reference recomputed fresh, then the table-served answer.
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto& q = queries[i];
            const auto res = index.find(q.key, query_mode);
            check(res, q);
            if (res.steps != q_steps[i]) throw std::runtime_error("query replay is not deterministic");
            const auto& reg = index.region(routes[i]);
            if (!reg.accepts_prefix(q.key)) {
                ++tally.fb_prefix;
                ++tally.fallback;
                continue;
            }
            const auto s = reg.residual(q.key);
            const auto ref = pfx2::reference_of(index, routes[i], q.key, res);
            tally.total_steps += ref.consumed;
            if (lits) continue;   // control arm: no PFX2 involvement
            ++tally.table_l1_reads;   // the candidate reads level 1 for every accepted suffix
            if (s.empty()) {
                ++tally.fb_empty;
                ++tally.fallback;
                continue;   // the table never participates; the production path already ran
            }
            const auto a = pfx2::serve(tables[routes[i]], s);
            if (a.kind == pfx2::Serve::Fallback) {
                ++tally.fallback;
                if (s.size() == 1) ++tally.fb_len1;
                else if (tables[routes[i]].row_of[static_cast<unsigned char>(s[0])] == pfx2::kNoRow)
                    ++tally.fb_row_missing;
                else {
                    ++tally.fb_row_entry;
                    ++tally.table_row_reads;   // a row was read and its entry was a fallback
                }
                continue;
            }
            if (a.consumed != ref.consumed)
                throw std::runtime_error("served length differs from the production path");
            if (a.group != ref.group)
                throw std::runtime_error("served group differs from the production path");
            const auto got = pfx2::fingerprint_of(s, a.consumed);
            if (got.state != ref.fp.state || got.tag != ref.fp.tag)
                throw std::runtime_error("served fingerprint differs from the production path");
            if (a.kind == pfx2::Serve::Level1) {
                ++tally.hit_l1;
                (res.found ? tally.hit_l1_found : tally.hit_l1_miss) += 1;
            } else {
                ++tally.hit_l2;
                (res.found ? tally.hit_l2_found : tally.hit_l2_miss) += 1;
            }
            tally.avoided_steps += a.consumed;
        }
        tally.queries_checked = queries.size();

        // Corpus differential: every stored key replays through the same comparison.
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto res = index.find(rows[i].key, query_mode);
            if (!res.found) throw std::runtime_error("corpus key not found during the differential");
            const auto& reg = index.region(row_routes[i]);
            if (!reg.accepts_prefix(rows[i].key))
                throw std::runtime_error("corpus key located in a region that rejects its prefix");
            if (lits) { ++tally.rows_checked; continue; }
            const auto s = reg.residual(rows[i].key);
            const auto ref = pfx2::reference_of(index, row_routes[i], rows[i].key, res);
            const auto a = pfx2::serve(tables[row_routes[i]], s);
            if (a.kind == pfx2::Serve::Fallback) {
                ++tally.rows_fallback;
            } else {
                if (a.consumed != ref.consumed || a.group != ref.group)
                    throw std::runtime_error("corpus differential: served answer differs");
                const auto got = pfx2::fingerprint_of(s, a.consumed);
                if (got.state != ref.fp.state || got.tag != ref.fp.tag)
                    throw std::runtime_error("corpus differential: served fingerprint differs");
                (a.kind == pfx2::Serve::Level1 ? tally.rows_l1 : tally.rows_l2) += 1;
            }
            ++tally.rows_checked;
        }

        // Space accounting for the planned flat representation.
        std::uint64_t regions_enrolled = 0, l2_rows_total = 0, capacity_slack = 0;
        for (const auto& t : tables)
            if (t.present) {
                ++regions_enrolled;
                l2_rows_total += t.rows.size();
                capacity_slack += (t.rows.capacity() - t.rows.size()) * 256 * 4;
            }
        const std::uint64_t l1_bytes = regions_enrolled * 256 * 8;
        const std::uint64_t row_index_bytes = regions_enrolled * 256 * 4;
        const std::uint64_t l2_bytes = l2_rows_total * 256 * 4;
        const std::uint64_t object_overhead_bytes = regions_enrolled ? regions_enrolled * 64 + 64 : 0;
        const std::uint64_t planned_bytes = l1_bytes + row_index_bytes + l2_bytes;
        const std::uint64_t resident_bytes = planned_bytes + object_overhead_bytes;
        const std::uint64_t generation_peak_bytes =
            resident_bytes + tally.scratch_max + 256 * 4 + 4096;
        const std::uint64_t index_bytes = index.memory();
        const bool gate_cover = (tally.hit_l1 + tally.hit_l2) * 10 >= queries.size() * 6;
        const bool gate_steps = tally.avoided_steps * 2 >= tally.total_steps;
        const bool gate_space = resident_bytes * 20 <= index_bytes;
        const bool gate_diff = tally.queries_checked == queries.size() &&
                               tally.rows_checked == rows.size() &&
                               (lits || tally.regions_enrolled == index.region_count()) &&
                               (lits || tally.l1_verified == tally.l1_determined) &&
                               (lits || tally.l2_verified == tally.l2_determined) &&
                               (lits || tally.edge_checked > 0);
        const bool entry_pass = !lits && gate_cover && gate_steps && gate_space && gate_diff;

        std::cout << "{\"backend\":\"" << argv[3]
                  << "\",\"enrolled\":" << (lits ? "false" : "true")
                  << ",\"rows\":" << rows.size() << ",\"queries\":" << queries.size()
                  << ",\"regions\":" << index.region_count()
                  << ",\"overflow_records\":" << index.overflow_count()
                  << ",\"index_bytes\":" << index_bytes
                  << ",\"prefix_rejected\":" << prefix_rejected
                  << ",\"lits_regions\":" << coverage.regions_lits
                  << ",\"lits_queries\":" << lits_queries
                  << ",\"checksum\":" << checksum << ",";
        by_source[0].emit("miss");
        std::cout << ',';
        by_source[1].emit("primary");
        std::cout << ',';
        by_source[2].emit("overflow");
#ifdef E25_DIAG
        std::cout << ",\"outer_locates\":" << counted.locate_calls
                  << ",\"context16_predictions\":" << counted.predict_calls
                  << ",\"fingerprint_bytes\":" << counted.fp_hash_bytes
                  << ",\"existence_searches\":" << counted.existence_searches
                  << ",\"full_compare_calls\":" << counted.cmp_calls
                  << ",\"logical_compare_bytes\":" << counted.cmp_bytes;
#endif
        std::cout << ',';
        pfx2::emit_hist("step_hist", tally.steps_all);
        std::cout << ',';
        pfx2::emit_hist("step_hist_found", tally.steps_found);
        std::cout << ',';
        pfx2::emit_hist("step_hist_miss", tally.steps_miss);
        std::cout << ",\"regions_enrolled\":" << tally.regions_enrolled
                  << ",\"l1_probes\":" << tally.l1_probes
                  << ",\"l1_determined\":" << tally.l1_determined
                  << ",\"l1_verified\":" << tally.l1_verified
                  << ",\"l1_undetermined\":" << tally.l1_undetermined
                  << ",\"l2_cells\":" << tally.l2_cells
                  << ",\"l2_determined\":" << tally.l2_determined
                  << ",\"l2_verified\":" << tally.l2_verified
                  << ",\"l2_rows\":" << tally.l2_rows
                  << ",\"hit_l1\":" << tally.hit_l1 << ",\"hit_l2\":" << tally.hit_l2
                  << ",\"hit_l1_found\":" << tally.hit_l1_found << ",\"hit_l1_miss\":" << tally.hit_l1_miss
                  << ",\"hit_l2_found\":" << tally.hit_l2_found << ",\"hit_l2_miss\":" << tally.hit_l2_miss
                  << ",\"fallback\":" << tally.fallback
                  << ",\"fb_prefix\":" << tally.fb_prefix
                  << ",\"fb_empty\":" << tally.fb_empty
                  << ",\"fb_len1\":" << tally.fb_len1
                  << ",\"fb_row_missing\":" << tally.fb_row_missing
                  << ",\"fb_row_entry\":" << tally.fb_row_entry
                  << ",\"avoided_steps\":" << tally.avoided_steps
                  << ",\"total_steps\":" << tally.total_steps
                  << ",\"table_l1_reads\":" << tally.table_l1_reads
                  << ",\"table_row_reads\":" << tally.table_row_reads
                  << ",\"queries_checked\":" << tally.queries_checked
                  << ",\"rows_checked\":" << tally.rows_checked
                  << ",\"rows_l1\":" << tally.rows_l1 << ",\"rows_l2\":" << tally.rows_l2
                  << ",\"rows_fallback\":" << tally.rows_fallback
                  << ",\"edge_checked\":" << tally.edge_checked
                  << ",\"edge_hits_l1\":" << tally.edge_hits_l1
                  << ",\"l1_bytes\":" << l1_bytes
                  << ",\"row_index_bytes\":" << row_index_bytes
                  << ",\"l2_bytes\":" << l2_bytes
                  << ",\"resident_bytes\":" << resident_bytes << ",\"planned_bytes\":" << planned_bytes
                  << ",\"object_overhead_bytes\":" << object_overhead_bytes
                  << ",\"capacity_slack_diagnostic\":" << capacity_slack
                  << ",\"generation_peak_bytes\":" << generation_peak_bytes
                  << ",\"space_fraction\":" << (double(resident_bytes) / double(index_bytes))
                  << ",\"gate_cover\":" << (lits ? "null" : (gate_cover ? "true" : "false"))
                  << ",\"gate_steps\":" << (lits ? "null" : (gate_steps ? "true" : "false"))
                  << ",\"gate_space\":" << (lits ? "null" : (gate_space ? "true" : "false"))
                  << ",\"gate_diff\":" << (gate_diff ? "true" : "false")
                  << ",\"entry_pass\":" << (entry_pass ? "true" : "false");
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
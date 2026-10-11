// GD prototype: the full point-query gate suite on one immutable index, then the
// pre-registered A/B from the plan's section 4.
//
// Arms (all four run on the SAME index state, only the geometry source and the
// boundary-directory switch differ):
//   arm 0 = B0 production geometry, locate off    arm 2 = G0 descriptor geometry, locate off
//   arm 1 = B1 production geometry, locate on     arm 3 = G1 descriptor geometry, locate on
// Main comparison, fixed before the run: arm2/arm0 (G0/B0) on Context16/tranco; the
// control pairing arm1/arm0 (two BASELINE arms differing only in `locate`) defines the
// spread floor. Arms 1 and 3 are reported, never substituted for the main pair.
//
// Counted/diagnostic builds run the gate suite; the clean build runs it too and then the
// timings; the sanitizer build runs it with repeats=0 and nothing else.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main

#include <map>
#include <set>
#include <stdexcept>

namespace gdproto {

using validation::Config;
using validation::Index;
using validation::Mode;
using validation::OvSearch;
using validation::Pair;
using validation::Prefetch;
using validation::Result;
using validation::Route;

inline Mode arm_mode(int arm) {
    Mode m;
    m.route = Route::Interval;
    m.prefetch = Prefetch::Off;
    m.fingerprint = true;
    m.simd = true;
    m.ov = OvSearch::Hybrid;
    m.ov_thr = 128;
    m.locate = (arm & 1) != 0;
    m.hot_geom = (arm & 2) != 0;
    return m;
}

struct Reference {
    std::vector<Pair> rows;
    void reset(std::vector<Pair> in) {
        rows = std::move(in);
        std::sort(rows.begin(), rows.end(),
                  [](const Pair& a, const Pair& b) { return compare(a.first, b.first) < 0; });
    }
    bool lookup(std::string_view key, std::uint64_t& value) const {
        const auto it = std::lower_bound(rows.begin(), rows.end(), key,
            [](const Pair& a, std::string_view b) { return compare(a.first, b) < 0; });
        if (it == rows.end() || compare(it->first, key) != 0) return false;
        value = it->second;
        return true;
    }
};

struct Gate {
    std::uint64_t arm_lookups = 0, oracle_mismatch = 0, arm_disagreement = 0,
                  counter_disagreement = 0;
    const char* phase = "corpus";
    std::vector<std::string> first_mismatch;
    void record(std::string_view key, int arm, const char* kind) {
        if (first_mismatch.size() >= 8) return;
        first_mismatch.push_back(std::string(phase) + ":" + kind + ":arm" + std::to_string(arm) +
                                 ":" + std::string(key.substr(0, 32)));
    }
    std::uint64_t corpus_keys = 0, corpus_queries = 0;
    std::uint64_t primary_hits = 0, overflow_hits = 0, misses = 0;
    std::uint64_t binary_keys = 0, binary_probes = 0;
    std::uint64_t layout_configs = 0, layout_keys = 0, layout_misses = 0, layout_writes = 0;
    std::uint64_t multiblock_groups = 0, max_blocks = 0, empty_blocks = 0;
    std::uint64_t field_groups = 0, field_mismatches = 0, field_mismatches_after_write = 0;
    std::uint64_t rebuild_keys = 0, rebuild_new_regions = 0;
    std::uint64_t injection_caught = 0, injection_clean = 0;
    std::uint64_t lits_in_domain = 0, lits_refusals = 0, lits_length_refusals = 0,
                  lits_length_skipped = 0;
    std::uint64_t e25_field_diffs = 0;
    std::uint64_t query_checksum = 0;
    std::uint64_t failures = 0;
};

inline void note_extra(const Result& r, Gate& g) {
    if (!r.found) {
        ++g.misses;
    } else if (r.source == 2) {
        ++g.overflow_hits;
    } else {
        ++g.primary_hits;
    }
}

// One key through all four arms. Every arm must equal the INDEPENDENT reference (not
// merely each other), and the four arms must agree on the answer, the source and every
// per-query counter -- a different group, CRC state, tag or address would move one of
// them, so this is where "same answer" and "same work" are separated from "same claim".
inline void check_key(const Index& index, std::string_view key, bool expect_found,
                      std::uint64_t expect_value, Gate& g) {
    Result r[4];
    for (int a = 0; a < 4; ++a) {
        r[a] = index.find(key, arm_mode(a));
        ++g.arm_lookups;
        if (r[a].found != expect_found || (expect_found && r[a].value != expect_value)) {
            ++g.oracle_mismatch;
            g.record(key, a, "oracle");
        }
    }
    // Only the PAIRS that differ in exactly one thing are compared: (0,2) and (1,3) are
    // baseline-vs-descriptor at the same `locate` setting. Arm 1 legitimately visits fewer
    // blocks than arm 0, so comparing across `locate` would count a designed difference.
    for (const auto& pair : {std::pair<int, int>{0, 2}, std::pair<int, int>{1, 3}}) {
        const auto& a = r[pair.first];
        const auto& b = r[pair.second];
        if (b.found != a.found || b.value != a.value || b.source != a.source) {
            ++g.arm_disagreement;
            g.record(key, pair.second, "answer");
        }
        if (b.steps != a.steps || b.subblocks_visited != a.subblocks_visited ||
            b.candidate_blocks != a.candidate_blocks ||
            b.simd_batches != a.simd_batches || b.tag_candidates != a.tag_candidates ||
            b.str_cmps_primary != a.str_cmps_primary ||
            b.str_cmps_overflow != a.str_cmps_overflow ||
            b.overflow_checks != a.overflow_checks ||
            b.overflow_entered != a.overflow_entered ||
            b.locate_probes != a.locate_probes || b.locate_cmp_bytes != a.locate_cmp_bytes) {
            ++g.counter_disagreement;
            g.record(key, pair.second, "counter");
        }
    }
    note_extra(r[0], g);
}

inline void check_key_ref(const Index& index, const Reference& ref, std::string_view key,
                          Gate& g) {
    std::uint64_t value = 0;
    const bool found = ref.lookup(key, value);
    check_key(index, key, found, value, g);
}

#ifdef E25_DIAG
// Descriptor fields against the live tables, and the layout census. After every write
// phase as well, because "the writes do not touch the geometry" is a claim the gate has
// to re-establish on the mutated state rather than inherit from the pristine one.
inline void verify_fields(const Index& index, Gate& g, bool after_write) {
    for (std::size_t r = 0; r < index.region_count(); ++r) {
        const auto& reg = index.region(r);
        const auto& sg = reg.slot_table();
        const auto& bb = reg.block_table();
        const auto& off = reg.offset_table();
        if (reg.diag_hot_geom_groups() != reg.groups()) ++g.field_mismatches;
        for (std::uint32_t grp = 0; grp < reg.groups(); ++grp) {
            ++g.field_groups;
            const std::size_t nb = reg.subblock_count(grp);
            bool ok = reg.diag_hot_geom_start(grp) == off[grp] &&
                      reg.diag_hot_geom_nb(grp) == nb &&
                      reg.diag_hot_geom_bb(grp) == bb[grp] &&
                      reg.diag_hot_geom_slots(grp) == sg[grp] &&
                      (off[grp + 1] - off[grp]) == nb * std::size_t(bb[grp]);
            for (std::size_t i = 0; i < nb && ok; ++i)
                ok = reg.diag_hot_geom_block_address(grp, i) == reg.diag_geom_block_address(grp, i);
            if (!ok) (after_write ? g.field_mismatches_after_write : g.field_mismatches) += 1;
            if (nb > g.max_blocks) g.max_blocks = nb;
            if (nb > 1) ++g.multiblock_groups;
            for (std::size_t i = 0; i < nb; ++i)
                if (reg.diag_subblock_count(grp, i) == 0) ++g.empty_blocks;
        }
    }
}
#endif

inline void prepare_all(const Index& index) {
    for (std::size_t r = 0; r < index.region_count(); ++r)
        if (!index.region(r).hot_geom_ready()) index.region(r).prepare_hot_geom();
}

inline Config gate_config(bool lits) {
    Config cfg = e25sw::baseline_config(128);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = lits ? validation::ModelBackend::LitsHpt
                             : validation::ModelBackend::Context16;
    cfg.compact_region_routing = true;   // C on; W/S/H stay off
    if (!cfg.compact_plan_commit || cfg.subblock_reclaim || cfg.reclaim_widest ||
        cfg.primary_hole_reuse || cfg.reclaim_low_activity || !cfg.offset_directory)
        throw std::runtime_error("configuration drift");
    return cfg;
}

inline std::vector<Pair> sorted_pairs(std::vector<Pair> in) {
    std::sort(in.begin(), in.end(),
              [](const Pair& a, const Pair& b) { return compare(a.first, b.first) < 0; });
    return in;
}

// --- gate 1: the recorded corpora, every key and every query, four arms ----------------
inline void gate_corpus(const Index& index, const Reference& ref,
                        const std::vector<unified::Query>& queries, Gate& g) {
    g.phase = "corpus";
    for (const auto& row : ref.rows) {
        check_key_ref(index, ref, row.first, g);
        ++g.corpus_keys;
    }
    for (const auto& q : queries) {
        std::uint64_t value = 0;
        const bool found = ref.lookup(q.key, value);
        if (found != q.expected_present || (found && value != q.expected_value))
            ++g.oracle_mismatch;   // the interchange oracle itself disagrees: device error
        check_key(index, q.key, found, value, g);
        ++g.corpus_queries;
    }
}

// --- gate 2: the full binary key domain, Context16 only -------------------------------
inline void gate_binary_domain(Gate& g) {
    Config cfg = gate_config(false);
    cfg.region_keys = 64;
    cfg.block_bytes = 256;
    cfg.slots = 4;
    cfg.fixed_groups = 2;   // deterministic dense groups: multi-block on purpose
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < 40; ++i) {
        std::string k(64, 'P');
        k.push_back(static_cast<char>(i / 256));
        k.push_back(static_cast<char>(i % 256));
        pairs.emplace_back(k, 1000 + i);
    }
    pairs.emplace_back("", 1);
    pairs.emplace_back(std::string("a\0", 2), 2);
    pairs.emplace_back(std::string("a\0\0", 3), 3);
    pairs.emplace_back(std::string(1, static_cast<char>(0)), 7);
    pairs.emplace_back(std::string(1, static_cast<char>(128)), 4);
    pairs.emplace_back(std::string(1, static_cast<char>(255)), 5);
    pairs.emplace_back(std::string(4096, 'z'), 6);
    for (std::size_t i = 0; i < 600; ++i) {
        std::string k = "k" + std::to_string(i);
        k.append(i % 17, 'x');
        pairs.emplace_back(k, 100 + i);
    }
    pairs = sorted_pairs(std::move(pairs));
    Index index(cfg);
    index.bulk_load(pairs);
    prepare_all(index);
    Reference ref;
    ref.reset(pairs);
    g.phase = "binary";
#ifdef E25_DIAG
    verify_fields(index, g, false);
#endif
    for (const auto& kv : pairs) {
        const std::string probes[3] = {kv.first, kv.first + std::string(1, '\0'),
                                       kv.first + std::string(1, static_cast<char>(255))};
        for (const auto& probe : probes) {
            check_key_ref(index, ref, probe, g);
            ++g.binary_probes;
        }
        ++g.binary_keys;
    }
}

// --- gate 3: layout variety, empty/full blocks, erase and insert -----------------------
inline void gate_layouts(Gate& g) {
    const struct { std::uint32_t bb; std::uint32_t slots; } combos[] = {
        {192, 2}, {256, 8}, {1024, 2}, {1024, 8}, {1024, 48}, {4096, 48}};
    for (const auto& combo : combos) {
        Config cfg = gate_config(false);
        cfg.block_bytes = combo.bb;
        cfg.slots = combo.slots;
        cfg.region_keys = 256;
        cfg.fixed_groups = 3;   // ~85 entries per group: multi-block whenever slots < 85
        std::vector<Pair> pairs;
        for (std::size_t i = 0; i < 1200; ++i) {
            std::string k = "layout";
            const std::size_t len = (i % 3 == 0) ? (1 + i % 3) : (i % 3 == 1 ? 20 : 200);
            k.append(len, static_cast<char>('a' + i % 26));
            k += std::to_string(i);
            pairs.emplace_back(k, 5000 + i);
        }
        pairs = sorted_pairs(std::move(pairs));
        Index index(cfg);
        index.bulk_load(pairs);
        prepare_all(index);
        Reference ref;
        ref.reset(pairs);
        g.phase = "layout";
#ifdef E25_DIAG
        verify_fields(index, g, false);
#endif
        for (const auto& kv : ref.rows) {
            check_key_ref(index, ref, kv.first, g);
            ++g.layout_keys;
        }
        std::vector<Pair> survivors;
        for (std::size_t i = 0; i < ref.rows.size(); ++i) {
            if (i % 4 == 0) {
                if (!index.erase(ref.rows[i].first)) ++g.failures;
                ++g.layout_writes;
            } else {
                survivors.push_back(ref.rows[i]);
            }
        }
        if (survivors.empty()) throw std::runtime_error("layout gate erased everything");
        ref.reset(survivors);
        for (const auto& kv : ref.rows) check_key_ref(index, ref, kv.first, g);
        for (const auto& kv : pairs) {
            std::uint64_t live = 0;
            if (ref.lookup(kv.first, live)) continue;   // still live: checked above
            check_key_ref(index, ref, kv.first, g);     // erased: must now miss
            ++g.layout_misses;
        }
#ifdef E25_DIAG
        verify_fields(index, g, true);
#endif
        std::vector<Pair> grown = ref.rows;
        for (std::size_t i = 0; i < 120; ++i) {
            std::string k = "inserted-" + std::to_string(i) + std::string(30, 'q');
            grown.emplace_back(k, 90000 + i);
            if (!index.insert(grown.back().first, grown.back().second)) ++g.failures;
            ++g.layout_writes;
        }
        std::size_t fresh = 0;
        for (std::size_t r = 0; r < index.region_count(); ++r)
            if (!index.region(r).hot_geom_ready()) ++fresh;
        g.rebuild_new_regions += fresh;
        prepare_all(index);   // a replaced region carries no descriptor until this runs
        ref.reset(grown);
        for (const auto& kv : ref.rows) check_key_ref(index, ref, kv.first, g);
        ++g.layout_configs;
    }
}

// --- gate 4: a real split must leave new regions that need a new descriptor ------------
inline void gate_rebuild(Gate& g) {
    Config cfg = gate_config(false);
    cfg.region_keys = 64;
    cfg.block_bytes = 1024;
    cfg.slots = 4;
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < 256; ++i) pairs.emplace_back("split-" + std::to_string(i), 7000 + i);
    pairs = sorted_pairs(std::move(pairs));
    Index index(cfg);
    index.bulk_load(pairs);
    prepare_all(index);
    for (std::size_t i = 0; i < 900; ++i) {
        const std::string k = "split-" + std::to_string(i + 256);
        if (!index.insert(k, 8000 + i)) ++g.failures;
        pairs.emplace_back(k, 8000 + i);
    }
    std::size_t unprepared = 0;
    for (std::size_t r = 0; r < index.region_count(); ++r)
        if (!index.region(r).hot_geom_ready()) ++unprepared;
    if (unprepared == 0) ++g.failures;   // the split must produce regions without a descriptor
    g.rebuild_new_regions += unprepared;
    prepare_all(index);
    Reference ref;
    ref.reset(pairs);
    g.phase = "rebuild";
    for (const auto& kv : ref.rows) {
        check_key_ref(index, ref, kv.first, g);
        ++g.rebuild_keys;
    }
#ifdef E25_DIAG
    verify_fields(index, g, true);
#endif
}

#ifdef E25_DIAG
// --- gate 5: a deliberately wrong descriptor must be REJECTED by the differential -------
//
// The injected pass is EXPECTED to disagree, so it keeps its own tallies; only the
// verdict ("the differential caught it" / "it is clean again after the restore") reaches
// the run gate. A candidate that never reads the descriptor would pass this trivially,
// which is exactly why the counted build also reports `gd_hot_geom_reads`.
inline void gate_injection(Gate& g) {
    Config cfg = gate_config(false);
    cfg.region_keys = 64;
    cfg.block_bytes = 1024;
    cfg.slots = 4;
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < 300; ++i) pairs.emplace_back("inject-" + std::to_string(i), 300 + i);
    pairs = sorted_pairs(std::move(pairs));
    Index index(cfg);
    index.bulk_load(pairs);
    prepare_all(index);
    Reference ref;
    ref.reset(pairs);
    bool injected = false;
    for (const auto& kv : ref.rows) {
        const std::size_t r = index.locate(kv.first);
        const auto& reg = index.region(r);
        const std::uint32_t grp = reg.diag_group(kv.first);
        if (grp == UINT32_MAX || grp >= reg.groups()) continue;
        const auto nb_true = reg.diag_hot_geom_nb(grp);
        if (nb_true == 0) continue;
        reg.diag_hot_geom_set_nb(grp, 0);   // wrong geometry, in bounds, decisive
        Gate dirty;
        dirty.phase = "injection";
        for (const auto& probe : ref.rows) check_key_ref(index, ref, probe.first, dirty);
        g.injection_caught += dirty.arm_disagreement;
        reg.diag_hot_geom_set_nb(grp, static_cast<std::uint32_t>(nb_true));
        Gate restored;
        restored.phase = "injection-restored";
        for (const auto& probe : ref.rows) check_key_ref(index, ref, probe.first, restored);
        if (restored.arm_disagreement == 0 && restored.oracle_mismatch == 0 &&
            restored.counter_disagreement == 0)
            ++g.injection_clean;
        else
            ++g.failures;
        injected = true;
        break;
    }
    if (!injected) ++g.failures;
}
#endif

// --- gate 6: the LITS input contract ---------------------------------------------------
inline void gate_lits(const Index& index, const std::vector<string_index_bench::v1::Row>& rows,
                      const std::vector<unified::Query>& queries, Gate& g) {
    for (const auto& row : rows) {
        if (lits_hpt::unsupported_reason(row.key) == nullptr) ++g.lits_in_domain;
        else ++g.failures;
    }
    for (const auto& q : queries) {
        if (lits_hpt::unsupported_reason(q.key) == nullptr) ++g.lits_in_domain;
        else ++g.failures;
    }
    const std::string high(1, static_cast<char>(128)), high2(1, static_cast<char>(255)),
                      nul("a\0b", 3), ascii("ascii-only");
    for (const auto& probe : {high, high2, nul, ascii}) {
        const bool refused = lits_hpt::unsupported_reason(probe) != nullptr;
        if (refused == (probe != ascii)) {
            if (refused) ++g.lits_refusals;
        } else {
            ++g.failures;
        }
    }
    // The length rule is enforced on the QUERY path: the model throws before it touches
    // the table. The probe therefore needs a key that reaches the model -- one region,
    // one shared prefix -- because a key outside the region prefix is rejected by the
    // index before the model is ever consulted, and would test nothing.
    Config cfg = gate_config(true);
    cfg.region_keys = 128;
    std::vector<Pair> probe_pairs;
    for (std::size_t i = 0; i < 64; ++i)
        probe_pairs.emplace_back("aaaprobe" + std::to_string(i), 4000 + i);
    probe_pairs = sorted_pairs(std::move(probe_pairs));
    Index probe_index(cfg);
    probe_index.bulk_load(probe_pairs);
    if (probe_index.region(0).uses_lits()) {
        std::string too_long("aaaprobe");
        too_long.resize(4097, 'z');   // 4097 bytes: one past the adapter's bound
        bool threw = false;
        try {
            (void)probe_index.find(too_long, arm_mode(0));
        } catch (const std::exception&) {
            threw = true;
        }
        if (threw) ++g.lits_length_refusals;
        else ++g.failures;
    } else {
        ++g.lits_length_skipped;   // the synthetic region fell back: no claim either way
    }
}
}  // namespace gdproto

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::invalid_argument("rows.bin queries.bin context16|lits repeats");
        const std::string backend = argv[3];
        const bool lits = backend == "lits";
        if (!lits && backend != "context16") throw std::invalid_argument("backend");
        const int repeats = std::stoi(argv[4]);
        if (repeats < 0 || repeats > 20 || repeats % 4)
            throw std::invalid_argument("repeats must be a multiple of 4 (balanced arms)");

        auto row_input = buffered_input(argv[1]);
        auto query_input = buffered_input(argv[2]);
        const auto rows = unified::read_rows(row_input);
        const auto queries = unified::read_queries(query_input);
        if (rows.empty() || queries.empty()) throw std::invalid_argument("empty input");

        Config cfg = gdproto::gate_config(lits);
        std::vector<Pair> pairs;
        pairs.reserve(rows.size());
        for (const auto& row : rows) pairs.emplace_back(row.key, row.value);
        gdproto::Reference ref;
        ref.reset(pairs);
        pairs = ref.rows;   // bulk_load takes sorted input

        const Mode modes[4] = {gdproto::arm_mode(0), gdproto::arm_mode(1),
                               gdproto::arm_mode(2), gdproto::arm_mode(3)};

        // ---- gate phase, on its own index ------------------------------------------
        gdproto::Gate gate;
        {
            Index index(cfg);
            index.bulk_load(pairs);
            gdproto::prepare_all(index);
            gdproto::gate_corpus(index, ref, queries, gate);
            for (const auto& q : queries)
                gate.query_checksum = gate.query_checksum * 131 + token(index.find(q.key, modes[0]));
            if (lits) gdproto::gate_lits(index, rows, queries, gate);
#ifdef E25_DIAG
            // Model / fingerprint / compare work per arm over the whole query sequence:
            // "the two arms did the same work" is measured, not asserted.
            std::uint64_t snap[4][4];
            auto take = [](std::uint64_t* out) {
                const auto& c = validation::e25::counts();
                out[0] = c.locate_calls;
                out[1] = c.predict_calls;
                out[2] = c.fp_hash_bytes;
                out[3] = c.cmp_bytes;
            };
            take(snap[0]);
            for (const auto& q : queries) (void)index.find(q.key, modes[0]);
            take(snap[1]);
            for (const auto& q : queries) (void)index.find(q.key, modes[2]);
            take(snap[2]);
            for (int f = 0; f < 4; ++f)
                if (snap[1][f] - snap[0][f] != snap[2][f] - snap[1][f]) ++gate.e25_field_diffs;
            for (const auto& q : queries) (void)index.find(q.key, modes[1]);
            for (const auto& q : queries) (void)index.find(q.key, modes[3]);
#endif
        }
        gdproto::gate_binary_domain(gate);
        gdproto::gate_layouts(gate);
        gdproto::gate_rebuild(gate);
#ifdef E25_DIAG
        gdproto::gate_injection(gate);
#endif
#ifdef E25_DIAG
        // The layout census, the descriptor-field check on mutated state and the injected
        // negative control all need the diagnostic accessors, so they are requirements of
        // the diagnostic build only. The timed build still runs every gate it can see.
        const bool census_ok = gate.multiblock_groups > 0 && gate.empty_blocks > 0 &&
            gate.field_mismatches == 0 && gate.field_mismatches_after_write == 0 &&
            gate.e25_field_diffs == 0 && gate.injection_caught > 0 && gate.injection_clean == 1;
#else
        const bool census_ok = true;
#endif
        const bool gate_pass = gate.failures == 0 && gate.oracle_mismatch == 0 &&
            gate.arm_disagreement == 0 && gate.counter_disagreement == 0 &&
            census_ok && gate.rebuild_new_regions > 0 &&
            gate.overflow_hits > 0 && gate.misses > 0 &&
            (lits ? (gate.lits_refusals == 3 && gate.lits_in_domain > 0 &&
                     gate.lits_length_refusals + gate.lits_length_skipped > 0)
                  : true);
        std::cout << "{\"phase\":\"gate\",\"backend\":\"" << backend << "\""
                  << ",\"rows\":" << rows.size() << ",\"queries\":" << queries.size()
                  << ",\"corpus_keys\":" << gate.corpus_keys
                  << ",\"corpus_queries\":" << gate.corpus_queries
                  << ",\"arm_lookups\":" << gate.arm_lookups
                  << ",\"oracle_mismatch\":" << gate.oracle_mismatch
                  << ",\"arm_disagreement\":" << gate.arm_disagreement
                  << ",\"counter_disagreement\":" << gate.counter_disagreement
                  << ",\"e25_field_diffs\":" << gate.e25_field_diffs
                  << ",\"primary_hits\":" << gate.primary_hits
                  << ",\"overflow_hits\":" << gate.overflow_hits
                  << ",\"misses\":" << gate.misses
                  << ",\"binary_keys\":" << gate.binary_keys
                  << ",\"binary_probes\":" << gate.binary_probes
                  << ",\"layout_configs\":" << gate.layout_configs
                  << ",\"layout_keys\":" << gate.layout_keys
                  << ",\"layout_misses\":" << gate.layout_misses
                  << ",\"layout_writes\":" << gate.layout_writes
                  << ",\"multiblock_groups\":" << gate.multiblock_groups
                  << ",\"max_blocks\":" << gate.max_blocks
                  << ",\"empty_blocks\":" << gate.empty_blocks
                  << ",\"field_groups\":" << gate.field_groups
                  << ",\"field_mismatches\":" << gate.field_mismatches
                  << ",\"field_mismatches_after_write\":" << gate.field_mismatches_after_write
                  << ",\"rebuild_keys\":" << gate.rebuild_keys
                  << ",\"rebuild_new_regions\":" << gate.rebuild_new_regions
                  << ",\"injection_caught\":" << gate.injection_caught
                  << ",\"injection_clean\":" << gate.injection_clean
                  << ",\"lits_in_domain\":" << gate.lits_in_domain
                  << ",\"lits_refusals\":" << gate.lits_refusals
                  << ",\"lits_length_refusals\":" << gate.lits_length_refusals
                  << ",\"lits_length_skipped\":" << gate.lits_length_skipped
                  << ",\"query_checksum\":" << gate.query_checksum
                  << ",\"first_mismatches\":[";
        for (std::size_t i = 0; i < gate.first_mismatch.size(); ++i)
            std::cout << (i ? "," : "") << "\"" << gate.first_mismatch[i] << "\"";
        std::cout << "]"
                  << ",\"failures\":" << gate.failures
                  << ",\"gate_pass\":" << (gate_pass ? "true" : "false")
                  << "}\n" << std::flush;
        if (repeats == 0) return gate_pass ? 0 : 1;

        // ---- build phase: two identical builds (A/A) plus the descriptor cost ------
        std::uint64_t load1 = 0, load2 = 0, prep = 0;
        {
            Index first(cfg);
            const auto t0 = std::chrono::steady_clock::now();
            first.bulk_load(pairs);
            load1 = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count());
        }
        std::unique_ptr<Index> index_holder(new Index(cfg));
        Index& index = *index_holder;
        {
            const auto t0 = std::chrono::steady_clock::now();
            index.bulk_load(pairs);
            load2 = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count());
        }
        {
            const auto t0 = std::chrono::steady_clock::now();
            gdproto::prepare_all(index);
            prep = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count());
        }
        std::uint64_t descriptor_bytes = 0;
        for (std::size_t r = 0; r < index.region_count(); ++r)
            descriptor_bytes += index.region(r).hot_geom_extra_bytes();
        std::cout << "{\"phase\":\"build\",\"backend\":\"" << backend << "\""
                  << ",\"load1_ns\":" << load1 << ",\"load2_ns\":" << load2
                  << ",\"prep_ns\":" << prep
                  << ",\"index_bytes\":" << index.memory()
                  << ",\"descriptor_bytes\":" << descriptor_bytes
                  << ",\"regions\":" << index.region_count() << "}\n" << std::flush;

        // ---- paired phase ----------------------------------------------------------
        std::uint64_t arm_checksum[4] = {0, 0, 0, 0};
        for (int a = 0; a < 4; ++a)
            for (const auto& q : queries) {
                const auto r = index.find(q.key, modes[a]);
                arm_checksum[a] = arm_checksum[a] * 131 + token(r);
            }
        if (arm_checksum[0] != arm_checksum[1] || arm_checksum[0] != arm_checksum[2] ||
            arm_checksum[0] != arm_checksum[3] || arm_checksum[0] != gate.query_checksum)
            throw std::runtime_error("arms disagree on the timed index");
        for (int rep = 0; rep < repeats; ++rep) {
            for (int pos = 0; pos < 4; ++pos) {
                const int arm = (rep + pos) % 4;
                std::uint64_t sum = 0;
                const auto start = std::chrono::steady_clock::now();
                for (int pass = 0; pass < 100; ++pass)
                    for (const auto& q : queries) {
                        const auto r = index.find(q.key, modes[arm]);
                        sum = sum * 131 + token(r);
                    }
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count();
                sink = sum;
                std::cout << "{\"phase\":\"paired\",\"backend\":\"" << backend << "\""
                          << ",\"repeat\":" << rep << ",\"position\":" << pos
                          << ",\"arm\":" << arm
                          << ",\"operations\":" << std::uint64_t(100) * queries.size()
                          << ",\"elapsed_ns\":" << elapsed << ",\"checksum\":" << sum << "}\n";
            }
        }
        std::cout << std::flush;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
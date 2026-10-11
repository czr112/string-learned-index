// GD entry diagnosis: six counted cells (two backends x three corpora).
//
// Candidate under test: docs/NEXT_ACTION_PLAN_20261011_GEOMETRY.md section 2 -- one
// contiguous 24-byte geometry descriptor per group replaces the four scattered reads
// (off_[g], off_[g+1], bb_[g], sg_[g]) and the division that re-derives the sub-block
// count on every point query.
//
// One immutable index; four arms per query on that same state:
//   b0 production geometry, locate off      g0 descriptor geometry, locate off
//   b1 production geometry, locate on       g1 descriptor geometry, locate on
// The four arms must agree on the answer (interchange oracle) AND on every per-query
// counter, because two sources of the same numbers must produce the same work. The
// device also compares, for every group of every region, the descriptor fields against
// the live tables and the block addresses both paths compute.
//
// This is a counter build: no timing is taken and no count converts to a speedup. The
// GD_SHAPE_PROBE build carries no counters; it exists so the disassembly check can read
// one symbol per arm, and its arm checksums must equal the counted build's.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main

#if !defined(E12_COUNT) && !defined(GD_SHAPE_PROBE)
#error GD entry requires E12_COUNT (counters only); only the shape build may omit it
#endif

#include <array>
#include <map>

namespace gd {

using validation::Index;
using validation::Mode;
using validation::OvSearch;
using validation::Prefetch;
using validation::Region;
using validation::Result;
using validation::Route;

inline Mode arm_mode(bool hot, bool locate) {
    Mode m;
    m.route = Route::Interval;
    m.prefetch = Prefetch::Off;
    m.fingerprint = true;
    m.simd = true;
    m.ov = OvSearch::Hybrid;
    m.ov_thr = 128;
    m.locate = locate;
    m.hot_geom = hot;
    return m;
}

struct CounterDiff {
    std::uint64_t steps = 0, subblocks_visited = 0, candidate_blocks = 0, simd_batches = 0,
                  tag_candidates = 0, str_cmps_primary = 0, str_cmps_overflow = 0,
                  overflow_checks = 0, overflow_entered = 0, locate_probes = 0,
                  locate_cmp_bytes = 0, source = 0, found = 0, value = 0;
    void add(const Result& a, const Result& b) {
        steps += a.steps != b.steps;
        subblocks_visited += a.subblocks_visited != b.subblocks_visited;
        candidate_blocks += a.candidate_blocks != b.candidate_blocks;
        simd_batches += a.simd_batches != b.simd_batches;
        tag_candidates += a.tag_candidates != b.tag_candidates;
        str_cmps_primary += a.str_cmps_primary != b.str_cmps_primary;
        str_cmps_overflow += a.str_cmps_overflow != b.str_cmps_overflow;
        overflow_checks += a.overflow_checks != b.overflow_checks;
        overflow_entered += a.overflow_entered != b.overflow_entered;
        locate_probes += a.locate_probes != b.locate_probes;
        locate_cmp_bytes += a.locate_cmp_bytes != b.locate_cmp_bytes;
        source += a.source != b.source;
        found += a.found != b.found;
        value += a.value != b.value;
    }
    std::uint64_t total() const {
        return steps + subblocks_visited + candidate_blocks + simd_batches + tag_candidates +
               str_cmps_primary + str_cmps_overflow + overflow_checks + overflow_entered +
               locate_probes + locate_cmp_bytes + source + found + value;
    }
};

}  // namespace gd

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::invalid_argument("rows.bin queries.bin context16|lits");
        auto row_input = buffered_input(argv[1]);
        auto query_input = buffered_input(argv[2]);
        const auto rows = unified::read_rows(row_input);
        const auto queries = unified::read_queries(query_input);
        if (rows.empty() || queries.empty()) throw std::invalid_argument("empty input");
        const std::string backend = argv[3];
        if (backend != "context16" && backend != "lits") throw std::invalid_argument("backend");
        const bool lits = backend == "lits";

        Config cfg = e25sw::baseline_config(128);
        e25sw::pin_frozen_switches(cfg);
        cfg.model_backend = lits ? ModelBackend::LitsHpt : ModelBackend::Context16;
        cfg.compact_region_routing = true;   // C on; W/S/H stay off (asserted below)
        if (!cfg.compact_plan_commit || cfg.subblock_reclaim || cfg.reclaim_widest ||
            cfg.primary_hole_reuse || cfg.reclaim_low_activity)
            throw std::runtime_error("configuration drift");
        if (!cfg.offset_directory || !cfg.multiblock)
            throw std::runtime_error("GD entry is fixed to the offset-directory layout");

        Index index(cfg);
        std::vector<Pair> pairs;
        pairs.reserve(rows.size());
        for (const auto& row : rows) pairs.emplace_back(row.key, row.value);
        index.bulk_load(pairs);
        std::vector<Pair>().swap(pairs);

        // Descriptor preparation belongs to the load, not to a warm-up: every region
        // is prepared before any query runs.
        for (std::size_t r = 0; r < index.region_count(); ++r)
            index.region(r).prepare_hot_geom();

        std::vector<std::size_t> routes(queries.size());
        std::uint64_t prefix_rejected = 0, lits_queries = 0;
        for (std::size_t i = 0; i < queries.size(); ++i) {
            routes[i] = index.locate(queries[i].key);
            const auto& reg = index.region(routes[i]);
            prefix_rejected += !reg.accepts_prefix(queries[i].key);
            lits_queries += reg.uses_lits();
        }
        const auto coverage = index.lits_coverage();

        const Mode b0m = gd::arm_mode(false, false), g0m = gd::arm_mode(true, false);
        const Mode b1m = gd::arm_mode(false, true), g1m = gd::arm_mode(true, true);

        // --- geometry differential: every group of every region -------------------
        std::uint64_t groups_total = 0, groups_checked = 0, field_mismatches = 0;
        std::uint64_t blocks_total = 0, address_pairs_checked = 0, address_mismatches = 0;
        std::uint64_t multi_block_groups = 0, regions_with_multiblock = 0;
        std::uint64_t descriptor_planned_bytes = 0, descriptor_capacity_bytes = 0,
                      descriptor_object_bytes = 0;
        std::uint64_t max_groups_per_region = 0, min_groups_per_region = ~std::uint64_t(0);
        std::uint64_t max_blocks_per_group = 0;
        std::array<std::uint64_t, 7> nb_hist{};
        std::map<std::uint64_t, std::uint64_t> bb_values, slots_values;
#ifdef E25_DIAG
        for (std::size_t r = 0; r < index.region_count(); ++r) {
            const Region& reg = index.region(r);
            const std::uint32_t groups = reg.groups();
            if (reg.diag_hot_geom_groups() != groups) ++field_mismatches;
            descriptor_planned_bytes += 24ull * groups;
            descriptor_capacity_bytes += reg.hot_geom_capacity_bytes();
            descriptor_object_bytes += reg.hot_geom_object_bytes();
            max_groups_per_region = std::max<std::uint64_t>(max_groups_per_region, groups);
            min_groups_per_region = std::min<std::uint64_t>(min_groups_per_region, groups);
            const auto& sg = reg.slot_table();
            const auto& bb = reg.block_table();
            const auto& off = reg.offset_table();
            bool region_multiblock = false;
            for (std::uint32_t g = 0; g < groups; ++g) {
                ++groups_total;
                const std::size_t nb_live = reg.subblock_count(g);
                const std::size_t nb_derived = (off[g + 1] - off[g]) / bb[g];
                bool ok = nb_live >= 1 && nb_live == nb_derived &&
                          (off[g + 1] - off[g]) == nb_live * std::size_t(bb[g]) &&
                          bb[g] % 64 == 0 && bb[g] <= 65535 && sg[g] > 0 &&
                          sg[g] <= reg.diag_max_slots() &&
                          reg.diag_hot_geom_start(g) == off[g] &&
                          reg.diag_hot_geom_nb(g) == nb_live &&
                          reg.diag_hot_geom_bb(g) == bb[g] &&
                          reg.diag_hot_geom_slots(g) == sg[g];
                if (!ok) ++field_mismatches;
                ++groups_checked;
                nb_hist[nb_live == 1 ? 0 : nb_live == 2 ? 1 : nb_live == 3 ? 2
                        : nb_live == 4 ? 3 : nb_live <= 8 ? 4 : nb_live <= 16 ? 5 : 6] += 1;
                if (nb_live > 1) {
                    ++multi_block_groups;
                    region_multiblock = true;
                }
                max_blocks_per_group = std::max<std::uint64_t>(max_blocks_per_group, nb_live);
                ++bb_values[bb[g]];
                ++slots_values[sg[g]];
                for (std::size_t i = 0; i < nb_live; ++i) {
                    ++blocks_total;
                    if (reg.diag_hot_geom_block_address(g, i) != reg.diag_geom_block_address(g, i))
                        ++address_mismatches;
                    else
                        ++address_pairs_checked;
                }
            }
            regions_with_multiblock += region_multiblock;
        }
#endif
        if (groups_total == 0) min_groups_per_region = 0;

        // --- query replay: four arms, same state, one answer -----------------------
        gd::CounterDiff counter_diffs;
        std::uint64_t queries_checked = 0;
        std::uint64_t arm_checksums[4] = {0, 0, 0, 0};
#ifdef E25_DIAG
        std::uint64_t hot_reads = 0, hot_reads_baseline = 0;
#endif
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto& q = queries[i];
#ifdef E25_DIAG
            auto mark = [](std::uint64_t& out) {
                out = validation::e25::counts().gd_hot_geom_reads;
            };
            std::uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
            mark(t0);
#endif
            const auto b0 = index.find(q.key, b0m);
#ifdef E25_DIAG
            mark(t1);
#endif
            const auto g0 = index.find(q.key, g0m);
#ifdef E25_DIAG
            mark(t2);
#endif
            const auto b1 = index.find(q.key, b1m);
#ifdef E25_DIAG
            mark(t3);
#endif
            const auto g1 = index.find(q.key, g1m);
#ifdef E25_DIAG
            mark(t4);
            hot_reads_baseline += (t1 - t0) + (t3 - t2);
            hot_reads += (t2 - t1) + (t4 - t3);
#endif
            check(b0, q);
            check(g0, q);
            check(b1, q);
            check(g1, q);
            counter_diffs.add(b0, g0);
            counter_diffs.add(b1, g1);
            arm_checksums[0] = arm_checksums[0] * 131 + token(b0);
            arm_checksums[1] = arm_checksums[1] * 131 + token(g0);
            arm_checksums[2] = arm_checksums[2] * 131 + token(b1);
            arm_checksums[3] = arm_checksums[3] * 131 + token(g1);
            ++queries_checked;
        }
        if (arm_checksums[0] != arm_checksums[1] || arm_checksums[0] != arm_checksums[2] ||
            arm_checksums[0] != arm_checksums[3])
            throw std::runtime_error("arms disagree on the query sequence");

        // --- corpus differential: every stored key through both geometries ---------
        gd::CounterDiff row_counter_diffs;
        std::uint64_t rows_checked = 0, row_value_mismatches = 0, row_checksum = 0;
        for (const auto& row : rows) {
            const auto b = index.find(row.key, b0m);
            const auto g = index.find(row.key, g0m);
            if (!b.found || !g.found || b.value != row.value || g.value != row.value)
                ++row_value_mismatches;
            row_counter_diffs.add(b, g);
            row_checksum = row_checksum * 131 + token(g);
            ++rows_checked;
        }

        // The descriptor object is one 24-byte, 8-byte aligned record per group; its
        // vector object lives inside Region (already counted by index.memory()) and its
        // buffer does not, so the resident figure is the new bytes plus that in-object
        // growth, and `index_bytes` double counts the in-object part by design -- the
        // gate is therefore a slight over-count, not an under-count.
        const std::uint64_t descriptor_slot_bytes = sizeof(Region::GeomSlot);
        const std::uint64_t descriptor_slot_align = alignof(Region::GeomSlot);
        if (descriptor_slot_bytes != 24 || descriptor_slot_align != 8)
            throw std::runtime_error("descriptor object is not 24 bytes / 8-aligned");
        const std::uint64_t descriptor_generation_scratch_bytes = 0;
        const std::uint64_t descriptor_resident_bytes =
            descriptor_capacity_bytes + descriptor_object_bytes;
        const std::uint64_t descriptor_generation_peak_bytes =
            descriptor_resident_bytes + descriptor_generation_scratch_bytes;
        const std::uint64_t index_bytes = index.memory();
        const bool gate_space = descriptor_resident_bytes * 20 <= index_bytes;
        const bool gate_fields = groups_checked == groups_total && field_mismatches == 0;
        const bool gate_address = address_pairs_checked == blocks_total && address_mismatches == 0;
        const bool gate_diff = queries_checked == queries.size() && rows_checked == rows.size() &&
                               row_value_mismatches == 0;
        const bool gate_counters = counter_diffs.total() == 0 && row_counter_diffs.total() == 0;
#ifdef E25_DIAG
        const bool gate_hot_reads = hot_reads > 0 && hot_reads_baseline == 0;
#else
        const std::uint64_t hot_reads = 0, hot_reads_baseline = 0;
        const bool gate_hot_reads = false;
#endif
        const bool entry_pass = gate_space && gate_fields && gate_address && gate_diff &&
                                gate_counters && gate_hot_reads;

#ifdef GD_SHAPE_PROBE
        // One odr-use per instantiation wrapper so the shape build carries one symbol
        // per arm. The values are discarded; this code exists only in GD_SHAPE_PROBE.
        sink = token(index.region(routes[0]).shape_probe_baseline(queries[0].key, b0m));
        sink = token(index.region(routes[0]).shape_probe_hot(queries[0].key, g0m));
#endif

        std::cout << "{\"backend\":\"" << backend << "\""
                  << ",\"rows\":" << rows.size()
                  << ",\"queries\":" << queries.size()
                  << ",\"regions\":" << index.region_count()
                  << ",\"groups_total\":" << groups_total
                  << ",\"overflow_records\":" << index.overflow_count()
                  << ",\"index_bytes\":" << index_bytes
                  << ",\"prefix_rejected\":" << prefix_rejected
                  << ",\"lits_regions\":" << coverage.regions_lits
                  << ",\"lits_queries\":" << lits_queries
                  << ",\"descriptor_slot_bytes\":" << descriptor_slot_bytes
                  << ",\"descriptor_slot_align\":" << descriptor_slot_align
                  << ",\"descriptor_planned_bytes\":" << descriptor_planned_bytes
                  << ",\"descriptor_capacity_bytes\":" << descriptor_capacity_bytes
                  << ",\"descriptor_object_bytes\":" << descriptor_object_bytes
                  << ",\"descriptor_resident_bytes\":" << descriptor_resident_bytes
                  << ",\"descriptor_generation_scratch_bytes\":" << descriptor_generation_scratch_bytes
                  << ",\"descriptor_generation_peak_bytes\":" << descriptor_generation_peak_bytes
                  << ",\"gate_space\":" << (gate_space ? "true" : "false")
                  << ",\"groups_checked\":" << groups_checked
                  << ",\"field_mismatches\":" << field_mismatches
                  << ",\"blocks_total\":" << blocks_total
                  << ",\"address_pairs_checked\":" << address_pairs_checked
                  << ",\"address_mismatches\":" << address_mismatches
                  << ",\"nb_hist\":[";
        for (std::size_t i = 0; i < nb_hist.size(); ++i)
            std::cout << (i ? "," : "") << nb_hist[i];
        std::cout << "],\"multi_block_groups\":" << multi_block_groups
                  << ",\"regions_with_multiblock\":" << regions_with_multiblock
                  << ",\"max_groups_per_region\":" << max_groups_per_region
                  << ",\"min_groups_per_region\":" << min_groups_per_region
                  << ",\"max_blocks_per_group\":" << max_blocks_per_group
                  << ",\"bb_values\":";
        auto emit_pairs = [](const std::map<std::uint64_t, std::uint64_t>& m) {
            std::cout << "[";
            bool first = true;
            for (const auto& kv : m) {
                std::cout << (first ? "" : ",") << "[" << kv.first << "," << kv.second << "]";
                first = false;
            }
            std::cout << "]";
        };
        emit_pairs(bb_values);
        std::cout << ",\"slots_values\":";
        emit_pairs(slots_values);
        std::cout << ",\"queries_checked\":" << queries_checked
                  << ",\"rows_checked\":" << rows_checked
                  << ",\"row_value_mismatches\":" << row_value_mismatches
                  << ",\"row_checksum\":" << row_checksum
                  << ",\"arm_checksums\":{\"b0\":" << arm_checksums[0]
                  << ",\"b1\":" << arm_checksums[2]
                  << ",\"g0\":" << arm_checksums[1]
                  << ",\"g1\":" << arm_checksums[3] << "}"
                  << ",\"counter_diffs\":{"
                  << "\"steps\":" << counter_diffs.steps
                  << ",\"subblocks_visited\":" << counter_diffs.subblocks_visited
                  << ",\"candidate_blocks\":" << counter_diffs.candidate_blocks
                  << ",\"simd_batches\":" << counter_diffs.simd_batches
                  << ",\"tag_candidates\":" << counter_diffs.tag_candidates
                  << ",\"str_cmps_primary\":" << counter_diffs.str_cmps_primary
                  << ",\"str_cmps_overflow\":" << counter_diffs.str_cmps_overflow
                  << ",\"overflow_checks\":" << counter_diffs.overflow_checks
                  << ",\"overflow_entered\":" << counter_diffs.overflow_entered
                  << ",\"locate_probes\":" << counter_diffs.locate_probes
                  << ",\"locate_cmp_bytes\":" << counter_diffs.locate_cmp_bytes
                  << ",\"source\":" << counter_diffs.source
                  << ",\"found\":" << counter_diffs.found
                  << ",\"value\":" << counter_diffs.value << "}"
                  << ",\"row_counter_diffs\":" << row_counter_diffs.total()
                  << ",\"hot_reads\":" << hot_reads
                  << ",\"hot_reads_baseline\":" << hot_reads_baseline
                  << ",\"checksum\":" << arm_checksums[1]
                  << ",\"gate_fields\":" << (gate_fields ? "true" : "false")
                  << ",\"gate_address\":" << (gate_address ? "true" : "false")
                  << ",\"gate_diff\":" << (gate_diff ? "true" : "false")
                  << ",\"gate_counters\":" << (gate_counters ? "true" : "false")
                  << ",\"gate_hot_reads\":" << (gate_hot_reads ? "true" : "false")
                  << ",\"entry_pass\":" << (entry_pass ? "true" : "false")
                  << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
// M2 entry: cross-sub-block contiguous tag panel, six counted cells, no timing.
//
// Device only -- production source is not touched and no candidate path exists yet. The
// panel is built and laid out for REAL (per-group, 32 B per block, tag start 16 B aligned)
// so the logical-line counts below are read off real addresses; it is an ADDRESS-LAYOUT
// PROXY, not a measured cache miss.
//
// For every query the device replays the production block loop from the panel:
// count, 16-slot SIMD batches, clamped live mask, lane order, the length check before the
// full compare, and the stop-on-found rule. Two independent tag sources are enumerated and
// compared item by item -- the panel copy and a recomputation from the stored suffix -- and
// both are checked against the production Result counters of the same query on the same
// index. Answers come from the interchange oracle; the corpus is checked key by key.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main

#include <set>

namespace contig {

using validation::Index;
using validation::Mode;
using validation::OvSearch;
using validation::Prefetch;
using validation::Region;
using validation::Result;
using validation::Route;

constexpr std::size_t kPanelBytes = 32;   // [0,16) count+reserved, [16,32) 16 tags
constexpr std::size_t kPanelTagOffset = 16;
constexpr std::size_t kLine = 64;

struct PanelTable {
    // One contiguous array per region, in group order, 32 B per block.
    std::vector<std::vector<std::byte>> bytes;
    std::vector<std::vector<std::uint32_t>> group_record;   // record index per group
    std::uint64_t planned_bytes = 0, capacity_bytes = 0, object_bytes = 0;

    const std::byte* record(std::size_t r, std::uint32_t g, std::size_t blk) const {
        return bytes[r].data() + std::size_t((group_record[r][g] + blk) * kPanelBytes);
    }
    std::uint16_t count(std::size_t r, std::uint32_t g, std::size_t blk) const {
        const std::byte* p = record(r, g, blk);
        std::uint16_t c = 0;
        std::memcpy(&c, p, sizeof(c));
        return c;
    }
    std::uint8_t tag(std::size_t r, std::uint32_t g, std::size_t blk, std::size_t slot) const {
        return static_cast<std::uint8_t>(record(r, g, blk)[kPanelTagOffset + slot]);
    }
};

inline std::uint64_t line_of(const void* address) {
    return reinterpret_cast<std::uintptr_t>(address) / kLine;
}

struct Tally {
    std::uint64_t queries = 0, queries_gt4 = 0;
    std::uint64_t orig_lines = 0, panel_lines = 0, cold_lines = 0, cold_blocks = 0, anchor_lines = 0;
    std::uint64_t sim_blocks = 0, sim_batches = 0, sim_candidates = 0, sim_candidate_blocks = 0,
                  sim_primary_cmps = 0;
    std::uint64_t counter_mismatch = 0, answer_mismatch = 0, sequence_mismatch = 0, oracle_mismatch = 0;
    std::uint64_t corpus_keys = 0, corpus_hits = 0, corpus_misses = 0, corpus_mismatch = 0;
    std::uint64_t panel_records = 0, align_violations = 0;
    std::uint64_t prod_blocks = 0, prod_batches = 0, prod_candidates = 0, prod_cand_blocks = 0,
                  prod_cmps = 0;
    std::array<std::uint64_t, 7> head_hist{};   // 1,2,3,4,5-8,9-16,>16
    std::vector<std::string> samples;
};

// One query: replay the production primary-block loop from the panel, collecting the
// metadata lines both layouts would touch, and compare against production.
inline void run_query(const Index& index, const PanelTable& panel, const std::string& key,
                      const Result& production, Tally& t) {
    ++t.queries;
    const std::size_t r = index.locate(key);
    const Region& reg = index.region(r);
    if (!reg.accepts_prefix(key)) {
        if (production.found) ++t.answer_mismatch;
        return;   // the production path returns immediately here; no lines, no panels
    }
    const std::string_view s = reg.residual(key);
    const std::uint32_t g = reg.diag_group(key);
    if (g == UINT32_MAX || g >= reg.groups()) {
        ++t.counter_mismatch;
        return;
    }
    const std::uint8_t tag = validation::Fingerprint::of(s);
    const std::size_t nb = reg.subblock_count(g);
    const std::size_t slots = reg.slot_table()[g];

    std::set<std::uint64_t> orig, pan, cold, cold_blocks_local;
    bool found = false;
    std::uint64_t sim_blocks = 0, sim_batches = 0, sim_candidates = 0, sim_cand_blocks = 0,
                  sim_cmps = 0;
    for (std::size_t blk = 0; blk < nb && !found; ++blk) {
        ++sim_blocks;
        ++sim_cand_blocks;
        const std::size_t count = panel.count(r, g, blk);
        if (count != reg.diag_subblock_count(g, blk)) ++t.counter_mismatch;
        orig.insert(line_of(reinterpret_cast<const void*>(reg.diag_block_address(g, blk))));
        const std::byte* pre = panel.record(r, g, blk);
        for (std::uint64_t line = line_of(pre); line <= line_of(pre + kPanelBytes - 1); ++line)
            pan.insert(line);
        for (std::size_t c = 0; c * 16 < slots; ++c) {
            const std::size_t lo = c * 16;
            const std::size_t top = std::min<std::size_t>(std::min<std::size_t>(slots, lo + 16), count);
            if (top <= lo) break;
            ++sim_batches;
            std::size_t mask = 0;
            for (std::size_t i = lo; i < top; ++i) {
                // panel tag and the tag recomputed from the stored suffix: independent
                // sources of the same byte, enumerated in the same lane order.
                const std::uint8_t stored = panel.tag(r, g, blk, i);
                const std::uint8_t derived =
                    validation::Fingerprint::of(reg.diag_suffix(g, blk, i));
                if (stored != derived) ++t.sequence_mismatch;
                if (stored == tag) mask |= std::size_t(1) << (i - lo);
            }
            sim_candidates += static_cast<std::uint64_t>(__builtin_popcountll(mask));
            for (std::size_t lane = 0; lane < top - lo && mask && !found; ++lane) {
                if (!(mask & (std::size_t(1) << lane))) continue;
                mask &= mask - 1;   // consumed in lane order, like the bit-twiddling loop
                cold.insert(line_of(reinterpret_cast<const void*>(
                    reg.diag_block_address(g, blk) + sizeof(validation::Header) + (lo + lane) * 16)));
                cold_blocks_local.insert(reg.diag_block_address(g, blk));
                const std::string stored_suffix = reg.diag_suffix(g, blk, lo + lane);
                if (stored_suffix.size() == s.size()) {
                    ++sim_cmps;
                    if (stored_suffix == s) found = true;
                }
            }
            if (found) break;
        }
    }
    const bool expect_primary = production.found && production.source == 1;
    if (found != expect_primary) ++t.answer_mismatch;
    if (sim_blocks != production.subblocks_visited || sim_batches != production.simd_batches ||
        sim_candidates != production.tag_candidates || sim_cand_blocks != production.candidate_blocks ||
        sim_cmps != production.str_cmps_primary) {
        ++t.counter_mismatch;
        if (t.samples.size() < 8)
            t.samples.push_back("sim(q)=" + std::to_string(sim_blocks) + "/" +
                                std::to_string(sim_batches) + "/" + std::to_string(sim_candidates) +
                                "/" + std::to_string(sim_cand_blocks) + "/" + std::to_string(sim_cmps) +
                                " prod=" + std::to_string(production.subblocks_visited) + "/" +
                                std::to_string(production.simd_batches) + "/" +
                                std::to_string(production.tag_candidates) + "/" +
                                std::to_string(production.candidate_blocks) + "/" +
                                std::to_string(production.str_cmps_primary));
    }
    if (production.overflow_entered) {
        ++t.anchor_lines;   // reported separately; never mixed into orig/panel
    }
    const std::uint64_t heads = orig.size();
    t.orig_lines += heads;
    t.panel_lines += pan.size();
    t.cold_blocks += cold_blocks_local.size();
    t.cold_lines += cold.size();
    t.sim_blocks += sim_blocks;
    t.sim_batches += sim_batches;
    t.sim_candidates += sim_candidates;
    t.sim_candidate_blocks += sim_cand_blocks;
    t.sim_primary_cmps += sim_cmps;
    t.prod_blocks += production.subblocks_visited;
    t.prod_batches += production.simd_batches;
    t.prod_candidates += production.tag_candidates;
    t.prod_cand_blocks += production.candidate_blocks;
    t.prod_cmps += production.str_cmps_primary;
    if (heads > 4) ++t.queries_gt4;
    t.head_hist[heads <= 4 ? heads - 1 : heads <= 8 ? 4 : heads <= 16 ? 5 : 6] += 1;
    if (t.samples.size() < 8 && t.queries <= 5)
        t.samples.push_back("q" + std::to_string(t.queries) + ":orig=" + std::to_string(orig.size()) +
                            " panel=" + std::to_string(pan.size()) +
                            " cold=" + std::to_string(cold.size()));
}

}  // namespace contig

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::invalid_argument("rows.bin queries.bin context16|lits");
        auto row_input = buffered_input(argv[1]);
        auto query_input = buffered_input(argv[2]);
        const auto rows = unified::read_rows(row_input);
        const auto queries = unified::read_queries(query_input);
        if (rows.empty() || queries.empty()) throw std::invalid_argument("empty input");
        const std::string backend = argv[3];
        const bool lits = backend == "lits";
        if (!lits && backend != "context16") throw std::invalid_argument("backend");

        Config cfg = e25sw::baseline_config(128);
        e25sw::pin_frozen_switches(cfg);
        cfg.model_backend = lits ? ModelBackend::LitsHpt : ModelBackend::Context16;
        cfg.compact_region_routing = true;
        if (!cfg.compact_plan_commit || cfg.subblock_reclaim || cfg.reclaim_widest ||
            cfg.primary_hole_reuse || cfg.reclaim_low_activity || !cfg.offset_directory ||
            cfg.slots != 16 || cfg.block_bytes != 1024)
            throw std::runtime_error("the M2 entry is fixed to the pinned layout");

        Index index(cfg);
        std::vector<Pair> pairs;
        pairs.reserve(rows.size());
        for (const auto& row : rows) pairs.emplace_back(row.key, row.value);
        index.bulk_load(pairs);
        std::vector<Pair>().swap(pairs);

        // ---- the panel table, laid out for real --------------------------------------
        contig::PanelTable panel;
        std::uint64_t built_records = 0, align_bad = 0;
        panel.bytes.resize(index.region_count());
        panel.group_record.resize(index.region_count());
        for (std::size_t r = 0; r < index.region_count(); ++r) {
            const Region& reg = index.region(r);
            std::uint64_t records = 0;
            panel.group_record[r].assign(reg.groups(), 0);
            for (std::uint32_t g = 0; g < reg.groups(); ++g) {
                panel.group_record[r][g] = static_cast<std::uint32_t>(records);
                records += reg.subblock_count(g);
            }
            panel.planned_bytes += records * contig::kPanelBytes;
            panel.bytes[r].resize(records * contig::kPanelBytes);
            panel.capacity_bytes += panel.bytes[r].capacity();
            panel.object_bytes += sizeof(panel.bytes[r]) + panel.group_record[r].capacity() * sizeof(std::uint32_t);
            for (std::uint32_t g = 0; g < reg.groups(); ++g) {
                const std::size_t nb = reg.subblock_count(g);
                for (std::size_t blk = 0; blk < nb; ++blk) {
                    const std::uint16_t count = static_cast<std::uint16_t>(reg.diag_subblock_count(g, blk));
                    std::byte* rec = panel.bytes[r].data() +
                                     std::size_t((panel.group_record[r][g] + blk) * contig::kPanelBytes);
                    std::memcpy(rec, &count, sizeof(count));
                    if (reinterpret_cast<std::uintptr_t>(rec) % 16 ||
                        (reinterpret_cast<std::uintptr_t>(rec) + contig::kPanelTagOffset) % 16)
                        ++align_bad;
                    for (std::size_t slot = 0; slot < count; ++slot) {
                        const std::string suffix = reg.diag_suffix(g, blk, slot);
                        rec[contig::kPanelTagOffset + slot] =
                            static_cast<std::byte>(validation::Fingerprint::of(suffix));
                    }
                    ++built_records;
                }
            }
        }

        // ---- corpus differential + per-query replay ----------------------------------
        contig::Tally tally;
        tally.panel_records = built_records;
        tally.align_violations = align_bad;
        std::vector<std::size_t> routes(queries.size());
        std::uint64_t prefix_rejected = 0, lits_queries = 0, checksum = 0;
        const Mode mode = ::mode(false);
        for (std::size_t i = 0; i < queries.size(); ++i) {
            const auto& q = queries[i];
            routes[i] = index.locate(q.key);
            const Region& reg = index.region(routes[i]);
            prefix_rejected += !reg.accepts_prefix(q.key);
            lits_queries += reg.uses_lits();
            const auto production = index.find(q.key, mode);
            check(production, q);
            checksum = checksum * 131 + token(production);
            contig::run_query(index, panel, q.key, production, tally);
        }
        for (const auto& row : rows) {
            const auto production = index.find(row.key, mode);
            if (!production.found || production.value != row.value) ++tally.corpus_mismatch;
            (production.found ? tally.corpus_hits : tally.corpus_misses) += 1;
            ++tally.corpus_keys;
        }
        const auto coverage = index.lits_coverage();
        const std::uint64_t index_bytes = index.memory();
        const std::uint64_t resident = panel.capacity_bytes + panel.object_bytes;
        const bool gate_heads = tally.queries_gt4 * 10 >= tally.queries * 6;   // >=60% of queries saw >4 headers
        const bool gate_lines = tally.panel_lines * 10 <= tally.orig_lines * 6;   // >=40% fewer
        const bool gate_space = resident * 20 <= index_bytes;
        const bool gate_diff = tally.counter_mismatch == 0 && tally.answer_mismatch == 0 &&
                               tally.sequence_mismatch == 0 && tally.oracle_mismatch == 0 &&
                               tally.corpus_mismatch == 0 && tally.queries == queries.size() &&
                               tally.corpus_keys == rows.size() && tally.align_violations == 0;
        const bool entry_pass = gate_heads && gate_lines && gate_space && gate_diff;
        std::cout << "{\"backend\":\"" << backend << "\""
                  << ",\"rows\":" << rows.size() << ",\"queries\":" << queries.size()
                  << ",\"regions\":" << index.region_count()
                  << ",\"prefix_rejected\":" << prefix_rejected
                  << ",\"lits_regions\":" << coverage.regions_lits
                  << ",\"lits_queries\":" << lits_queries
                  << ",\"checksum\":" << checksum
                  << ",\"index_bytes\":" << index_bytes
                  << ",\"panel_planned_bytes\":" << panel.planned_bytes
                  << ",\"panel_capacity_bytes\":" << panel.capacity_bytes
                  << ",\"panel_object_bytes\":" << panel.object_bytes
                  << ",\"panel_resident_bytes\":" << resident
                  << ",\"queries_gt4\":" << tally.queries_gt4
                  << ",\"orig_lines\":" << tally.orig_lines
                  << ",\"panel_lines\":" << tally.panel_lines
                  << ",\"cold_blocks\":" << tally.cold_blocks
                  << ",\"cold_lines\":" << tally.cold_lines
                  << ",\"anchor_lines\":" << tally.anchor_lines
                  << ",\"sim_blocks\":" << tally.sim_blocks
                  << ",\"sim_batches\":" << tally.sim_batches
                  << ",\"sim_candidates\":" << tally.sim_candidates
                  << ",\"sim_candidate_blocks\":" << tally.sim_candidate_blocks
                  << ",\"sim_primary_cmps\":" << tally.sim_primary_cmps
                  << ",\"panel_records\":" << tally.panel_records
                  << ",\"align_violations\":" << tally.align_violations
                  << ",\"prod_blocks\":" << tally.prod_blocks
                  << ",\"prod_batches\":" << tally.prod_batches
                  << ",\"prod_candidates\":" << tally.prod_candidates
                  << ",\"prod_candidate_blocks\":" << tally.prod_cand_blocks
                  << ",\"prod_cmps\":" << tally.prod_cmps
                  << ",\"counter_mismatch\":" << tally.counter_mismatch
                  << ",\"answer_mismatch\":" << tally.answer_mismatch
                  << ",\"sequence_mismatch\":" << tally.sequence_mismatch
                  << ",\"corpus_keys\":" << tally.corpus_keys
                  << ",\"corpus_hits\":" << tally.corpus_hits
                  << ",\"corpus_misses\":" << tally.corpus_misses
                  << ",\"corpus_mismatch\":" << tally.corpus_mismatch
                  << ",\"head_hist\":[";
        for (std::size_t i = 0; i < tally.head_hist.size(); ++i)
            std::cout << (i ? "," : "") << tally.head_hist[i];
        std::cout << "],\"samples\":[";
        for (std::size_t i = 0; i < tally.samples.size(); ++i)
            std::cout << (i ? "," : "") << "\"" << tally.samples[i] << "\"";
        std::cout << "]"
                  << ",\"gate_heads\":" << (gate_heads ? "true" : "false")
                  << ",\"gate_lines\":" << (gate_lines ? "true" : "false")
                  << ",\"gate_space\":" << (gate_space ? "true" : "false")
                  << ",\"gate_diff\":" << (gate_diff ? "true" : "false")
                  << ",\"entry_pass\":" << (entry_pass ? "true" : "false")
                  << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
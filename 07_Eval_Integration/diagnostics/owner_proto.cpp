// M3 prototype: gates first, then the pre-registered A/B.
//
// Arms (same data, same query sequence):
//   P = production: every region trains and keeps its Context16 reference table
//   A = P again: the A/A control pairing that defines each phase's spread floor
//   S = prototype: a LITS-mapped region does not train and does not keep the table
//
// Gate phase (diagnostic build): answers and per-query counters equal across P/S, the
// saving measured against the production index's own accounting, the saving must be the
// WHOLE theoretical table bytes of LITS-mapped regions, fallback regions keep their table,
// the reference-model API refuses on a table-less region, and a split under the prototype
// still skips the table for the new LITS regions.
//
// Timing phase (clean build): build / query / update / scan, paired inside one process and
// repeat, with the A/A control defining the floor. No counter build takes timings.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main

#include <chrono>
#include <memory>

namespace ownerproto {

using validation::Config;
using validation::Index;
using validation::Mode;
using validation::OvSearch;
using validation::Pair;
using validation::Prefetch;
using validation::Result;
using validation::Route;

struct Tally {
    std::uint64_t corpus_keys = 0, corpus_mismatch = 0, queries = 0, answer_mismatch = 0,
                  counter_mismatch = 0;
    std::uint64_t regions_p = 0, lits_p = 0, fallback_p = 0;
    std::uint64_t model_bytes_p = 0, model_bytes_s = 0, theoretical_lits_bytes = 0;
    std::uint64_t model_bytes_lits_p = 0, model_bytes_fallback_p = 0;
    std::uint64_t lits_with_table_s = 0, fallback_without_table_s = 0;
    std::uint64_t group_of_refusals = 0, split_new_regions = 0, split_lits_with_table = 0;
    std::uint64_t erase_work_ops = 0, scan_rows_p = 0, scan_rows_s = 0, scan_mismatch = 0;
    std::uint64_t update_mismatch = 0;
    std::uint64_t checksum = 0;
};

inline bool same_result(const Result& a, const Result& b) {
    return a.found == b.found && a.value == b.value && a.source == b.source &&
           a.steps == b.steps && a.subblocks_visited == b.subblocks_visited &&
           a.candidate_blocks == b.candidate_blocks && a.simd_batches == b.simd_batches &&
           a.tag_candidates == b.tag_candidates && a.str_cmps_primary == b.str_cmps_primary &&
           a.str_cmps_overflow == b.str_cmps_overflow && a.overflow_checks == b.overflow_checks &&
           a.overflow_entered == b.overflow_entered && a.locate_probes == b.locate_probes &&
           a.locate_cmp_bytes == b.locate_cmp_bytes;
}

inline Config proto_config(bool lits, bool skip) {
    Config cfg = e25sw::baseline_config(128);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = lits ? validation::ModelBackend::LitsHpt
                             : validation::ModelBackend::Context16;
    cfg.compact_region_routing = true;
    cfg.lits_skip_reference_table = skip;
    return cfg;
}

inline std::uint64_t region_model_bytes(const Index& index) {
    std::uint64_t total = 0;
    for (std::size_t r = 0; r < index.region_count(); ++r)
        total += index.region(r).memory_breakdown().model;
    return total;
}

inline std::uint64_t region_lits_model_bytes(const Index& index) {
    std::uint64_t total = 0;
    for (std::size_t r = 0; r < index.region_count(); ++r)
        if (index.region(r).uses_lits()) total += index.region(r).memory_breakdown().model;
    return total;
}

}  // namespace ownerproto

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::invalid_argument("rows.bin queries.bin context16|lits repeats");
        const std::string backend = argv[3];
        const bool lits = backend == "lits";
        if (!lits && backend != "context16") throw std::invalid_argument("backend");
        const int repeats = std::stoi(argv[4]);
        if (repeats != 0 && (repeats < 3 || repeats > 12 || repeats % 3))
            throw std::invalid_argument("repeats must be 0 or a multiple of 3");

        auto row_input = buffered_input(argv[1]);
        auto query_input = buffered_input(argv[2]);
        const auto rows = unified::read_rows(row_input);
        const auto queries = unified::read_queries(query_input);
        if (rows.empty() || queries.empty()) throw std::invalid_argument("empty input");

        std::vector<Pair> pairs;
        pairs.reserve(rows.size());
        for (const auto& row : rows) pairs.emplace_back(row.key, row.value);
        std::vector<std::pair<std::string, std::uint64_t>> sorted_ref(pairs.begin(), pairs.end());
        std::sort(sorted_ref.begin(), sorted_ref.end(),
                  [](const auto& a, const auto& b) { return compare(a.first, b.first) < 0; });

        const Mode mode = ::mode(false);
        ownerproto::Tally gate;

        // ---- gate phase: one production index and one prototype index -----------------
        std::unique_ptr<Index> production(new Index(ownerproto::proto_config(lits, false)));
        std::unique_ptr<Index> prototype(new Index(ownerproto::proto_config(lits, true)));
        production->bulk_load(pairs);
        prototype->bulk_load(pairs);

        gate.regions_p = production->region_count();
        const std::uint64_t index_bytes_pristine = production->memory();
        gate.model_bytes_p = ownerproto::region_model_bytes(*production);
        gate.model_bytes_lits_p = ownerproto::region_lits_model_bytes(*production);
        gate.model_bytes_fallback_p = gate.model_bytes_p - gate.model_bytes_lits_p;
        gate.model_bytes_s = ownerproto::region_model_bytes(*prototype);
        gate.theoretical_lits_bytes = ownerproto::region_lits_model_bytes(*production);
        for (std::size_t r = 0; r < production->region_count(); ++r) {
            const auto& reg = production->region(r);
            if (reg.uses_lits()) ++gate.lits_p;
            else ++gate.fallback_p;
        }
        for (std::size_t r = 0; r < prototype->region_count(); ++r) {
            const auto& reg = prototype->region(r);
            const bool resident = reg.reference_table_resident();
            if (reg.uses_lits()) {
                if (resident) ++gate.lits_with_table_s;
            } else if (!resident) {
                ++gate.fallback_without_table_s;
            }
        }

        for (const auto& q : queries) {
            const auto a = production->find(q.key, mode);
            const auto b = prototype->find(q.key, mode);
            check(a, q);
            ++gate.queries;
            gate.checksum = gate.checksum * 131 + token(a);
            if (!ownerproto::same_result(a, b) || b.found != a.found || b.value != a.value ||
                token(a) != token(b))
                ++gate.answer_mismatch;
        }
        for (const auto& row : rows) {
            const auto a = production->find(row.key, mode);
            const auto b = prototype->find(row.key, mode);
            ++gate.corpus_keys;
            if (!a.found || a.value != row.value) ++gate.corpus_mismatch;
            if (!ownerproto::same_result(a, b) || b.found != a.found || b.value != a.value)
                ++gate.answer_mismatch;
        }
        // The reference-model API must refuse when the table is absent, not read an empty one.
        if (gate.lits_p > 0) {
            for (std::size_t r = 0; r < prototype->region_count(); ++r)
                if (prototype->region(r).uses_lits()) {
                    bool threw = false;
                    try {
                        (void)prototype->region(r).group_of("m3-probe");
                    } catch (const std::exception&) {
                        threw = true;
                    }
                    if (threw) ++gate.group_of_refusals;
                    break;
                }
        }
        // A real split under the prototype: the new regions must skip the table too.
        {
            std::size_t before = prototype->region_count();
            for (std::size_t i = 0; i < 400; ++i) {
                const std::string k = "m3-split-" + std::to_string(i);
                (void)prototype->insert(k, 900000 + i);
            }
            for (std::size_t r = 0; r < prototype->region_count(); ++r) {
                const auto& reg = prototype->region(r);
                if (reg.uses_lits() && !reg.reference_table_resident()) ++gate.split_new_regions;
                if (reg.uses_lits() && reg.reference_table_resident()) ++gate.split_lits_with_table;
            }
            gate.split_new_regions += prototype->region_count() - before;
        }
        // Erase and scan on both arms must agree; counting is the gate, not timing.
        {
            std::vector<Pair> out_p, out_s;
            for (std::size_t i = 0; i < 500; ++i) {
                const auto& key = rows[(i * 37) % rows.size()].key;
                const bool ep = prototype->erase(key);
                const bool eprod = production->erase(key);
                ++gate.erase_work_ops;
                if (ep != eprod) ++gate.update_mismatch;
            }
            for (std::size_t i = 0; i < 40; ++i) {
                const auto& key = rows[(i * 611) % rows.size()].key;
                const std::size_t r_p = production->locate(key);
                const std::size_t r_s = prototype->locate(key);
                out_p.clear();
                out_s.clear();
                production->region(r_p).scan(key, 64, out_p);
                prototype->region(r_s).scan(key, 64, out_s);
                gate.scan_rows_p += out_p.size();
                gate.scan_rows_s += out_s.size();
                if (out_p != out_s) ++gate.scan_mismatch;
            }
        }
        const std::uint64_t saving = gate.model_bytes_p - gate.model_bytes_s;
        const bool gate_answers = gate.answer_mismatch == 0 && gate.corpus_mismatch == 0 &&
                                  gate.queries == queries.size() && gate.corpus_keys == rows.size();
        const bool gate_tables = gate.lits_with_table_s == 0 && gate.fallback_without_table_s == 0;
        const bool gate_saving = lits ? (saving == gate.theoretical_lits_bytes &&
                                         saving * 10 >= gate.theoretical_lits_bytes * 9)
                                      : (saving == 0);
        const bool gate_refusal = gate.lits_p == 0 || gate.group_of_refusals == 1;
        const bool gate_split = gate.split_lits_with_table == 0;
        const bool gate_work = gate.update_mismatch == 0 && gate.scan_mismatch == 0 &&
                               gate.scan_rows_p == gate.scan_rows_s;
        const bool gate_pass = gate_answers && gate_tables && gate_saving && gate_refusal &&
                               gate_split && gate_work;
        std::cout << "{\"phase\":\"gate\",\"backend\":\"" << backend << "\""
                  << ",\"rows\":" << rows.size() << ",\"queries\":" << queries.size()
                  << ",\"regions\":" << gate.regions_p << ",\"regions_lits\":" << gate.lits_p
                  << ",\"regions_fallback\":" << gate.fallback_p
                  << ",\"model_bytes_production\":" << gate.model_bytes_p
                  << ",\"model_bytes_prototype\":" << gate.model_bytes_s
                  << ",\"theoretical_lits_bytes\":" << gate.theoretical_lits_bytes
                  << ",\"saving_bytes\":" << saving
                  << ",\"model_bytes_lits_production\":" << gate.model_bytes_lits_p
                  << ",\"model_bytes_fallback_production\":" << gate.model_bytes_fallback_p
                  << ",\"pristine_index_bytes\":" << index_bytes_pristine
                  << ",\"saving_ratio_of_index\":"
                  << (index_bytes_pristine ? (double)saving / (double)index_bytes_pristine : 0.0)
                  << ",\"lits_with_table_prototype\":" << gate.lits_with_table_s
                  << ",\"fallback_without_table_prototype\":" << gate.fallback_without_table_s
                  << ",\"group_of_refusals\":" << gate.group_of_refusals
                  << ",\"split_regions\":\"" << gate.split_new_regions << "/"
                  << gate.split_lits_with_table << "\""
                  << ",\"erase_work_ops\":" << gate.erase_work_ops
                  << ",\"scan_rows\":" << gate.scan_rows_p
                  << ",\"answer_mismatch\":" << gate.answer_mismatch
                  << ",\"corpus_mismatch\":" << gate.corpus_mismatch
                  << ",\"update_mismatch\":" << gate.update_mismatch
                  << ",\"scan_mismatch\":" << gate.scan_mismatch
                  << ",\"checksum\":" << gate.checksum
                  << ",\"gate_answers\":" << (gate_answers ? "true" : "false")
                  << ",\"gate_tables\":" << (gate_tables ? "true" : "false")
                  << ",\"gate_saving\":" << (gate_saving ? "true" : "false")
                  << ",\"gate_refusal\":" << (gate_refusal ? "true" : "false")
                  << ",\"gate_split\":" << (gate_split ? "true" : "false")
                  << ",\"gate_work\":" << (gate_work ? "true" : "false")
                  << ",\"gate_pass\":" << (gate_pass ? "true" : "false")
                  << "}\n" << std::flush;
        if (repeats == 0) return gate_pass ? 0 : 1;

        // ---- timing phase: build / query / update / scan on P, A, S --------------------
        std::unique_ptr<Index> arm[3];
        std::vector<Pair> work = pairs;
        std::uint64_t checksum[4][3] = {{0}};
        auto now = [] { return std::chrono::steady_clock::now(); };
        auto ns = [](const std::chrono::steady_clock::time_point& a,
                     const std::chrono::steady_clock::time_point& b) {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
        };

        auto build_arm = [&](int a) {
            arm[a].reset(new Index(ownerproto::proto_config(lits, a == 2)));
            const auto t0 = now();
            arm[a]->bulk_load(pairs);
            return ns(t0, now());
        };
        auto query_work = [&](int a) {
            std::uint64_t sum = 0;
            for (int pass = 0; pass < 100; ++pass)
                for (const auto& q : queries) sum = sum * 131 + token(arm[a]->find(q.key, mode));
            return sum;
        };
        auto update_work = [&](int a) {
            std::uint64_t sum = 1;
            for (std::size_t i = 0; i < 2000; ++i) {
                const std::string k = "m3-timing-" + std::to_string(i);
                sum = sum * 131 + (arm[a]->insert(k, i) ? 1u : 0u);
            }
            for (std::size_t i = 0; i < 2000; ++i) {
                const std::string k = "m3-timing-" + std::to_string(i);
                sum = sum * 131 + (arm[a]->erase(k) ? 1u : 0u);
            }
            return sum;
        };
        auto scan_work = [&](int a) {
            std::uint64_t sum = 1;
            std::vector<Pair> out;
            for (std::size_t i = 0; i < 200; ++i) {
                const auto& key = rows[(i * 977) % rows.size()].key;
                out.clear();
                arm[a]->region(arm[a]->locate(key)).scan(key, 64, out);
                for (const auto& kv : out) sum = sum * 131 + kv.second;
            }
            return sum;
        };
        const char* kinds[4] = {"build", "query", "update", "scan"};
        for (int rep = 0; rep < repeats; ++rep) {
            for (int pos = 0; pos < 3; ++pos) {
                const int a = (rep + pos) % 3;
                for (int kind = 0; kind < 4; ++kind) {
                    std::uint64_t elapsed = 0, sum = 0;
                    const auto t0 = now();
                    if (kind == 0) {
                        elapsed = build_arm(a);
                    } else {
                        if (!arm[a]) { build_arm(a); }
                        if (kind == 1) sum = query_work(a);
                        else if (kind == 2) sum = update_work(a);
                        else sum = scan_work(a);
                        elapsed = ns(t0, now());
                    }
                    checksum[kind][a] = checksum[kind][a] * 131 + sum;
                    std::cout << "{\"phase\":\"timing\",\"backend\":\"" << backend
                              << "\",\"kind\":\"" << kinds[kind] << "\",\"repeat\":" << rep
                              << ",\"position\":" << pos << ",\"arm\":" << a
                              << ",\"operations\":"
                              << (kind == 1 ? 100ull * queries.size() : kind == 2 ? 4000ull
                                  : kind == 3 ? 200ull : 0ull)
                              << ",\"elapsed_ns\":" << elapsed << ",\"checksum\":" << sum
                              << "}\n" << std::flush;
                }
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
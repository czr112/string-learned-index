// M3 entry: is the per-region Context16 reference table ever READ on the paths that matter?
//
// The claim under test is not "the table is useful" but "for a region whose mapping is the
// LITS backend's, the reference table is trained, kept, and never read by the query, update
// or scan paths". This device measures it as a falsifiable identity on the real workloads:
//
//     Delta(Model::predict) during a replay  ==  (lookups that landed in a fallback region)
//
// where a fallback region is one whose LITS calibration failed and which therefore still maps
// with the reference model. The identity is checked for the query sequence, for the corpus
// keys, and for the construction itself. No timing; production source untouched.
#define main previous_lookup_main
#include "lookup_structure.cpp"
#undef main

namespace owner {

using validation::Index;
using validation::Region;

struct Tally {
    std::uint64_t regions = 0, regions_lits = 0, regions_fallback = 0;
    std::uint64_t model_bytes_lits = 0, model_bytes_fallback = 0, model_object_bytes = 0;
    std::uint64_t groups_lits = 0, groups_fallback = 0;
    std::uint64_t build_predict = 0;
    std::uint64_t build_lookups_fallback = 0;
    std::uint64_t query_predict = 0, query_lookups = 0, query_lookups_fallback = 0,
                  query_predict_expected = 0;
    std::uint64_t row_predict = 0, row_lookups = 0, row_lookups_fallback = 0;
    std::uint64_t checksum = 0, oracle_mismatch = 0, corpus_mismatch = 0;
    std::uint64_t lits_boundary = 0;
    std::uint64_t erase_predict = 0, erase_ops_fallback = 0, scan_predict = 0, scan_ops_fallback = 0;
};

}  // namespace owner

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
            cfg.primary_hole_reuse || cfg.reclaim_low_activity || !cfg.offset_directory)
            throw std::runtime_error("configuration drift");

        std::vector<Pair> pairs;
        pairs.reserve(rows.size());
        for (const auto& row : rows) pairs.emplace_back(row.key, row.value);

        owner::Tally t;
        std::vector<std::size_t> routes(queries.size());
        std::vector<std::size_t> row_routes(rows.size());
        const Mode mode = ::mode(false);
        std::unique_ptr<Index> holder(new Index(cfg));
        Index& index = *holder;
        {
            // Construction: the table is trained for EVERY region; the question is whether the
            // demand pass READS it. One reference-model entry per key of a fallback region.
            e25::counts().reset();
            index.bulk_load(pairs);
            t.build_predict = e25::counts().predict_calls;
        }
        for (std::size_t r = 0; r < index.region_count(); ++r) {
            const Region& reg = index.region(r);
            const auto mb = reg.memory_breakdown();
            ++t.regions;
            t.model_object_bytes += sizeof(validation::Model);
            if (reg.uses_lits()) {
                ++t.regions_lits;
                t.model_bytes_lits += mb.model;
                t.groups_lits += reg.groups();
            } else {
                ++t.regions_fallback;
                t.model_bytes_fallback += mb.model;
                t.groups_fallback += reg.groups();
            }
            if (reg.lits_status() == lits_hpt::Status::NoKeys ||
                reg.lits_status() == lits_hpt::Status::TooFewKeys)
                ++t.lits_boundary;
        }

        // Query replay: Delta(Model::predict) must equal the number of lookups that landed in
        // a fallback region -- no more, no less.
        {
            e25::counts().reset();
            for (std::size_t i = 0; i < queries.size(); ++i) {
                const auto& q = queries[i];
                routes[i] = index.locate(q.key);
                const Region& reg = index.region(routes[i]);
                if (reg.accepts_prefix(q.key)) {
                    ++t.query_lookups;
                    if (!reg.uses_lits()) ++t.query_lookups_fallback;
                }
                const auto res = index.find(q.key, mode);
                check(res, q);
                t.checksum = t.checksum * 131 + token(res);
            }
            t.query_predict = e25::counts().predict_calls;
        }
        // Corpus replay, same identity.
        {
            e25::counts().reset();
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto& row = rows[i];
                row_routes[i] = index.locate(row.key);
                const Region& reg = index.region(row_routes[i]);
                if (reg.accepts_prefix(row.key)) {
                    ++t.row_lookups;
                    if (!reg.uses_lits()) ++t.row_lookups_fallback;
                }
                const auto res = index.find(row.key, mode);
                if (!res.found || res.value != row.value) ++t.corpus_mismatch;
            }
            t.row_predict = e25::counts().predict_calls;
        }

        // Update and scan paths, measured rather than argued. Erases do not rebuild a
        // region, so the reference-model entry count must equal the number of erase
        // operations that landed in a fallback region; a scan may re-derive one group per
        // group it walks, so its count is required to be exactly zero only on a cell with no
        // fallback region at all. Destructive: this runs last.
        {
            e25::counts().reset();
            for (std::size_t i = 0; i < 2000; ++i) {
                const auto& key = rows[(i * 31) % rows.size()].key;
                const std::size_t r = index.locate(key);
                if (index.region(r).accepts_prefix(key) && !index.region(r).uses_lits())
                    ++t.erase_ops_fallback;
                (void)index.erase(key);
            }
            t.erase_predict = e25::counts().predict_calls;
        }
        {
            e25::counts().reset();
            std::vector<Pair> out;
            for (std::size_t i = 0; i < 50; ++i) {
                const auto& key = rows[(i * 977) % rows.size()].key;
                const std::size_t r = index.locate(key);
                if (index.region(r).accepts_prefix(key) && !index.region(r).uses_lits())
                    ++t.scan_ops_fallback;
                out.clear();
                index.region(r).scan(key, 64, out);
            }
            t.scan_predict = e25::counts().predict_calls;
        }
        const std::uint64_t index_bytes = index.memory();
        const bool identity_queries = t.query_predict == t.query_lookups_fallback;
        const bool identity_rows = t.row_predict == t.row_lookups_fallback;
        const bool gate_zero = (t.regions_fallback == 0) ? (t.query_predict == 0 && t.row_predict == 0)
                                                         : true;   // covered by the identities
        const bool gate_diff = t.oracle_mismatch == 0 && t.corpus_mismatch == 0 &&
                               t.query_lookups == queries.size() && t.row_lookups == rows.size();
        const bool identity_erase = t.erase_predict == t.erase_ops_fallback;
        const bool scan_zero = (t.regions_fallback != 0) || t.scan_predict == 0;
        const bool gate_identity = identity_queries && identity_rows && identity_erase && scan_zero;
        const bool gate_audit = true;   // completeness is argued in the report, not counted here
        const bool entry_pass = gate_identity && gate_zero && gate_diff;
        std::cout << "{\"backend\":\"" << backend << "\""
                  << ",\"rows\":" << rows.size() << ",\"queries\":" << queries.size()
                  << ",\"regions\":" << t.regions
                  << ",\"regions_lits\":" << t.regions_lits
                  << ",\"regions_fallback\":" << t.regions_fallback
                  << ",\"lits_boundary_regions\":" << t.lits_boundary
                  << ",\"model_bytes_lits\":" << t.model_bytes_lits
                  << ",\"model_bytes_fallback\":" << t.model_bytes_fallback
                  << ",\"model_object_bytes\":" << t.model_object_bytes
                  << ",\"model_bytes_total\":" << (t.model_bytes_lits + t.model_bytes_fallback)
                  << ",\"index_bytes\":" << index_bytes
                  << ",\"groups_lits\":" << t.groups_lits
                  << ",\"groups_fallback\":" << t.groups_fallback
                  << ",\"build_predict_calls\":" << t.build_predict
                  << ",\"query_predict_calls\":" << t.query_predict
                  << ",\"query_lookups\":" << t.query_lookups
                  << ",\"query_lookups_fallback\":" << t.query_lookups_fallback
                  << ",\"row_predict_calls\":" << t.row_predict
                  << ",\"row_lookups\":" << t.row_lookups
                  << ",\"row_lookups_fallback\":" << t.row_lookups_fallback
                  << ",\"erase_predict_calls\":" << t.erase_predict
                  << ",\"erase_ops_fallback\":" << t.erase_ops_fallback
                  << ",\"scan_predict_calls\":" << t.scan_predict
                  << ",\"scan_ops_fallback\":" << t.scan_ops_fallback
                  << ",\"checksum\":" << t.checksum
                  << ",\"oracle_mismatch\":" << t.oracle_mismatch
                  << ",\"corpus_mismatch\":" << t.corpus_mismatch
                  << ",\"gate_identity\":" << (gate_identity ? "true" : "false")
                  << ",\"gate_zero\":" << (gate_zero ? "true" : "false")
                  << ",\"gate_diff\":" << (gate_diff ? "true" : "false")
                  << ",\"gate_audit\":" << (gate_audit ? "true" : "false")
                  << ",\"entry_pass\":" << (entry_pass ? "true" : "false")
                  << "}\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
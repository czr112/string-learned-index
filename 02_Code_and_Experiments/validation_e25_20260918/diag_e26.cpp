// E26 diagnostics: what compaction actually does, and whether reusing the stored
// tag changes anything besides the hashing.
//
// Three questions from the review, answered from the counters rather than inferred:
//
//   1. Why is compact() entered, and does it repeatedly run without enabling the
//      placement that triggered it? `compact_entered` counts entries into the branch;
//      `compact_helped` / `compact_no_help` split the retry outcome. A "no help" is
//      recorded as "did not enable THIS placement" -- compact() also redistributes
//      records across sub-blocks, so it is not the same claim as "the compaction was
//      worthless".
//   2. How much GARBAGE sits in the groups that are actually under pressure? Not the
//      index-wide total: only the groups whose placement failed and whose compaction
//      therefore ran.
//   3. How much would a compaction have to copy, and how much does reusing the stored
//      tag save? `compact_bytes_copied` is the copy volume; the difference in
//      `fp_hash_bytes` between C0 and C1 is the hashing removed.
//
// C1 must not change membership, placement order, capacity, the trigger, or boundary
// maintenance. With -DE25_DIAG the compaction also checks every reused tag against
// the recomputed one and counts any mismatch; a non-zero count is a hard failure of
// the reuse argument, so this program asserts it is zero.
//
// Build with -DE25_DIAG. This is a diagnostic build; the timed C0/C1 comparison is a
// separate clean binary.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;
using Counts = validation::e25::Counts;

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

struct Op {
    int kind;
    std::string key;
    std::uint64_t value;
};

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("diag_e26 KIND N [TRANCOCSV] [RUNTAG]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
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

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        c.multiblock = true;
        c.reuse_group_ids = true;
        c.ov_threshold = thr;
        c.write_fast_locate = true;      // both arms sit on the E25 default candidate
        c.reuse_locate_ctx = true;
        Config c0 = c;                    // C0: existing compaction
        Config c1 = c; c1.compact_reuse_tag = true;   // C1: reuse the stored tag

        Pipeline plan;
        {
            Index probe(c);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        const std::size_t quarter = N / 4;
        std::vector<Op> ins, rep;
        for (std::size_t j = 0; j < quarter; ++j)
            ins.push_back({1, keys[(j * 7) % N].first + "~new" + std::to_string(j), 0x1000ULL + j});
        for (std::size_t j = 0; j < quarter; ++j) {
            rep.push_back({2, keys[(j * 3) % N].first, 0});
            rep.push_back({1, keys[(j * 3) % N].first + "~rep" + std::to_string(j), 0x2000ULL + j});
        }

        std::ofstream out("results/main/e26_diag.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);
        auto& C = validation::e25::counts();

        const char* wlnames[2] = {"insert_only", "replace"};
        const std::vector<Op>* wlops[2] = {&ins, &rep};

        auto run = [&](const char* arm, const Config& cfg, const char* wl, const std::vector<Op>& ops) {
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;
            auto s = std::make_unique<Index>(cfg);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

            for (const auto& o : ops) {
                if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert target present"); ref[o.key] = o.value; }
                else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase target absent"); }
            }

            C.reset();
            for (const auto& o : ops) {
                if (o.kind == 1) s->insert(o.key, o.value);
                else s->erase(o.key);
            }
            const Counts k = C;
            if (s->size() != ref.size()) throw std::runtime_error("size drift vs reference");

            out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << wl
                << "\",\"arm\":\"" << arm << "\",\"run_tag\":\"" << run_tag << "\""
                << ",\"live\":" << s->size()
                << ",\"compactions\":" << s->compaction_count()
                << ",\"compact_entered\":" << k.compact_entered
                << ",\"compact_helped\":" << k.compact_helped
                << ",\"compact_no_help\":" << k.compact_no_help
                << ",\"compact_records_replayed\":" << k.compact_records_replayed
                << ",\"compact_bytes_copied\":" << k.compact_bytes_copied
                << ",\"compact_garbage_before\":" << k.compact_garbage_before
                << ",\"compact_tag_mismatch\":" << k.compact_tag_mismatch
                << ",\"fp_hash_bytes\":" << k.fp_hash_bytes
                << ",\"fp_of_calls\":" << k.fp_of_calls
                << ",\"predict_calls\":" << k.predict_calls
                << ",\"cmp_calls\":" << k.cmp_calls
                << ",\"overflow_after\":" << s->overflow_count()
                << "}" << std::endl;

            // The reuse argument fails outright if a stored tag ever disagreed with
            // the recomputed one. Asserted, not merely reported.
            if (k.compact_tag_mismatch != 0)
                throw std::runtime_error("reused tag disagreed with the recomputed tag");

            std::cerr << kind << " " << wl << " " << arm << " done (compact "
                      << k.compact_entered << " entered / " << k.compact_helped
                      << " helped)" << std::endl;
        };

        for (int w = 0; w < 2; ++w) {
            run("C0", c0, wlnames[w], *wlops[w]);
            run("C1", c1, wlnames[w], *wlops[w]);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

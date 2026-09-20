// E27 diagnostics: what a range scan spends its work on.
//
// The review named four quantities. This program measures all four SEPARATELY so the
// round can pick the one worth changing instead of optimizing by guesswork:
//
//   1. start location   -- outer routing calls per scan (Index::locate is re-evaluated
//                          inside Index::scan's loop, so this is not 1 per scan)
//   2. empty groups / sub-blocks stepped over
//   3. ordering comparisons in the primary-vs-overflow merge
//   4. result materialisation -- string constructions per returned record
//
// Semantics are E23's, unchanged: from the first record with key >= start, return AT
// MOST n records in key order as complete key/value pairs. The start list and the
// states (initial / insert-25% / replace-25%) are E23's, and the scan lengths are read
// from the protocol rather than remembered.
//
// Build with -DE25_DIAG. No timings here: this round picks a target first, and only
// then does a same-round comparison against the frozen version and the sorted array.

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
        if (argc < 3) throw std::invalid_argument("diag_scan KIND N [TRANCOCSV] [RUNTAG]");
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
        // The scan round sits on the E25/E26 default candidate, so what it measures is
        // the state the project would actually ship.
        c.write_fast_locate = true;
        c.reuse_locate_ctx = true;
        c.compact_reuse_tag = true;
        c.compact_needs_garbage = true;

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

        // E23's start list: four fifths real keys, one fifth synthetic keys that exist
        // in no bucket, so "start falls between two keys" is exercised.
        std::vector<std::string> starts;
        {
            std::mt19937_64 rng(2026091801);
            for (int i = 0; i < 4096; ++i) {
                const std::string& k = keys[rng() % N].first;
                if (i % 5 == 0) starts.push_back(k + "~between");
                else starts.push_back(k);
            }
        }
        const std::size_t NS[3] = {16, 128, 1024};

        std::ofstream out("results/main/e27_scan_diag.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);
        auto& C = validation::e25::counts();

        struct State { const char* name; const std::vector<Op>* ops; };
        const State states[3] = {{"initial", nullptr}, {"insert25", &ins}, {"replace25", &rep}};

        for (const auto& st : states) {
            static const std::vector<Op> none;
            const std::vector<Op>& ops = st.ops ? *st.ops : none;

            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;
            for (const auto& o : ops) {
                if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert present"); ref[o.key] = o.value; }
                else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase absent"); }
            }

            auto s = std::make_unique<Index>(c);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            for (const auto& o : ops) {
                if (o.kind == 1) s->insert(o.key, o.value);
                else s->erase(o.key);
            }
            if (s->size() != ref.size()) throw std::runtime_error("size drift");

            for (std::size_t ni = 0; ni < 3; ++ni) {
                const std::size_t n = NS[ni];
                C.reset();
                std::uint64_t returned = 0, short_scans = 0, min_ret = SIZE_MAX;
                std::uint64_t content_checked = 0;
                for (const auto& k : starts) {
                    const auto got = s->scan(k, n);
                    returned += got.size();
                    min_ret = std::min(min_ret, got.size());
                    if (got.size() < n) ++short_scans;
                    // Correctness outside the counted window is not possible here (the
                    // counters are global), so the CONTENT is verified instead: every
                    // returned record must be what the reference says, in order.
                    std::size_t idx = 0;
                    for (auto it = ref.lower_bound(k); it != ref.end() && idx < got.size(); ++it, ++idx) {
                        ++content_checked;
                        if (it->first != got[idx].first || it->second != got[idx].second)
                            throw std::runtime_error("scan content mismatch");
                    }
                    if (idx != got.size()) throw std::runtime_error("scan length mismatch");
                }
                const Counts k = C;
                out << "{\"kind\":\"" << kind << "\",\"state\":\"" << st.name
                    << "\",\"n\":" << n << ",\"run_tag\":\"" << run_tag << "\""
                    << ",\"scans\":" << starts.size()
                    << ",\"returned\":" << returned
                    << ",\"short_scans\":" << short_scans
                    << ",\"min_returned\":" << (min_ret == SIZE_MAX ? 0 : min_ret)
                    << ",\"verified_records\":" << content_checked
                    << ",\"locate_calls\":" << k.locate_calls
                    << ",\"scan_regions\":" << k.scan_regions
                    << ",\"scan_groups\":" << k.scan_groups
                    << ",\"scan_groups_empty\":" << k.scan_groups_empty
                    << ",\"scan_blocks_skipped\":" << k.scan_blocks_skipped
                    << ",\"scan_merge_cmps\":" << k.scan_merge_cmps
                    << ",\"scan_result_allocs\":" << k.scan_result_allocs
                    << "}" << std::endl;
                std::cerr << kind << " " << st.name << " n=" << n << " done" << std::endl;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

// E31 gate: the in-place sub-block reclaim must not change WHAT the index holds.
//
// The reclaim packs a sub-block's live suffixes toward the block end and updates their
// offsets. Descriptors keep their slots, the sub-block's key range is unchanged, and
// the boundary directory is not rebuilt -- so the change is a byte movement inside one
// sub-block and nothing else.
//
// What must hold:
//   * CONTENT and KEY ORDER are identical to the frozen version and to the reference,
//     element by element, at every checkpoint (not by checksum);
//   * the reclaim branch actually RAN. The element-wise comparison proves the state is
//     equivalent; it does not prove the new code was reached, and a gate that passes
//     without executing the branch under test certifies nothing. The counters are
//     process-global and only the E31 arm can increment them (the frozen arm has the
//     switch off), so `subblock_reclaims > 0` at the last checkpoint is exactly the
//     statement "the branch ran". Asserted, not printed and ignored. This is why the
//     gate is built WITH -DE25_DIAG: a gate is not a timing build.
//   * the boundary directory needs no rebuild. repack_subblock moves bytes and writes
//     only Entry::offset and Header::bottom, so the claim follows from the code -- but
//     "follows from the code" is not a measurement, so every multi-block group's
//     directory is re-checked against its own sub-blocks' minimum keys at every
//     checkpoint. An earlier version of this comment claimed the first/last slot
//     invariant was "asserted directly"; it was not, and it does not need to be, since
//     descriptors are never permuted -- the directory check is the observable
//     consequence that is worth testing.
//
// Note on scope: this gate runs the `same` (equal-length) workload. `grow` is a
// different workload and is run as a separate invocation, into its own log.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

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

// E31: the reclaim claims the boundary directory needs no rebuild because descriptors
// keep their slots and only their byte offsets move. Re-check that claim directly:
// for every multi-block group, the directory must still list exactly the non-empty
// sub-blocks, in order, and each entry must still be that sub-block's minimum key.
// Returns the number of multi-block groups checked, so the log states how much was
// actually compared instead of leaving "it passed" to imply full coverage.
static std::uint64_t check_directory(const Index& idx) {
    std::uint64_t groups_checked = 0;
    for (std::size_t i = 0; i < idx.region_count(); ++i) {
        const auto& r = idx.region(i);
        for (std::uint32_t g = 0; g < r.groups(); ++g) {
            const std::size_t nb = r.subblock_count(g);
            if (nb < 2) continue;
            ++groups_checked;
            const auto dir = r.boundary_dir(g);
            std::size_t rank = 0;
            for (std::size_t k = 0; k < nb; ++k) {
                if (r.subblock_entries(g, k) == 0) continue;
                if (rank >= dir.size() || dir[rank].second != k)
                    throw std::runtime_error("boundary directory no longer lists this sub-block");
                const auto keys = r.subblock_keys(g, k);
                if (keys.empty() || keys.front() != dir[rank].first)
                    throw std::runtime_error("boundary is no longer the sub-block minimum");
                ++rank;
            }
            if (dir.size() != rank)
                throw std::runtime_error("directory size != non-empty sub-blocks");
        }
    }
    return groups_checked;
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_e31 KIND N [TRANCOCSV] [RUNTAG] [MODE]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const std::string mode = argc > 5 ? argv[5] : "same";
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

        Config frozen = e25sw::baseline_config(thr);
        frozen.write_fast_locate = true;
        frozen.reuse_locate_ctx = true;
        frozen.scan_reserve_result = true;
        frozen.compact_reuse_tag = true;
        frozen.compact_needs_garbage = true;
        Config c31 = frozen;
        c31.subblock_reclaim = true;
        e25sw::require_only_switches_differ(frozen, c31, {"subblock_reclaim"});
        std::cerr << "arms frozen[" << e25sw::switches(frozen) << "] E31["
                  << e25sw::switches(c31) << "]" << std::endl;

        Pipeline plan;
        {
            Index probe(frozen);
            probe.bulk_load(keys);
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        std::deque<std::string> live;
        std::unordered_set<std::string> live_set;
        Reference ref;
        for (const auto& kv : keys) { live.push_back(kv.first); ref[kv.first] = kv.second; live_set.insert(kv.first); }

        auto a = std::make_unique<Index>(frozen);
        auto b = std::make_unique<Index>(c31);
        a->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
        b->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        std::uint64_t serial = 0, compared = 0;
        std::size_t done = 0;
        const double CPS[4] = {0.25, 0.50, 1.00, 2.00};
        for (double frac : CPS) {
            const std::size_t target = static_cast<std::size_t>(double(N) * frac);
            while (done < target) {
                const std::string victim = live.front();
                live.pop_front();
                live_set.erase(victim);
                std::string base = victim;
                const auto pos = base.find("~r");
                if (pos != std::string::npos) base.resize(pos);
                std::string fresh;
                if (mode == "grow") {
                    fresh = base + "~r" + std::to_string(serial);
                } else {
                    fresh = base;
                    bool ok = false;
                    for (int t = 0; t < 64 && !fresh.empty(); ++t) {
                        fresh.back() = static_cast<char>('a' + ((serial + t) % 26));
                        if (fresh != victim && ref.find(fresh) == ref.end() &&
                            live_set.find(fresh) == live_set.end()) { ok = true; break; }
                    }
                    if (!ok) throw std::runtime_error("alphabet exhausted");
                }
                const std::uint64_t val = 0x7000ULL + serial;
                ++serial;
                ref.erase(victim); ref[fresh] = val;
                a->erase(victim); a->insert(fresh, val);
                b->erase(victim); b->insert(fresh, val);
                live.push_back(fresh); live_set.insert(fresh);
                ++done;
            }

            // Full content comparison, element by element, against each other AND the
            // reference. Agreeing on a wrong answer is not a pass.
            const auto ca = a->scan(std::string_view{}, a->size());
            const auto cb = b->scan(std::string_view{}, b->size());
            if (ca.size() != ref.size() || cb.size() != ref.size())
                throw std::runtime_error("size != reference");
            for (std::size_t i = 0; i < ca.size(); ++i) {
                ++compared;
                if (ca[i].first != cb[i].first || ca[i].second != cb[i].second)
                    throw std::runtime_error("E31 content or order differs at " + std::to_string(i) +
                                             " key=" + ca[i].first);
                const auto it = ref.find(ca[i].first);
                if (it == ref.end() || it->second != ca[i].second)
                    throw std::runtime_error("disagrees with reference: " + ca[i].first);
            }
            // E31: assert that the branch under test ran, and that the boundary
            // directory is still consistent. The directory is re-checked on BOTH arms,
            // so a failure cannot be blamed on the new code by default.
#ifdef E25_DIAG
            const std::uint64_t reclaims = validation::e25::counts().subblock_reclaims;
            const std::uint64_t reclaim_helped = validation::e25::counts().subblock_reclaim_helped;
#else
#error "check_e31 must be built with -DE25_DIAG: it asserts that the reclaim branch ran"
#endif
            const std::uint64_t dir_groups = check_directory(*a) + check_directory(*b);
            if (frac == 2.00 && reclaims == 0)
                throw std::runtime_error(
                    "the reclaim branch never ran: this gate verified nothing about it");

            std::cout << "{\"kind\":\"" << kind << "\",\"mode\":\"" << mode << "\""
                      << ",\"frac\":" << frac << ",\"live\":" << ref.size()
                      << ",\"frozen_overflow\":" << a->overflow_count()
                      << ",\"e31_overflow\":" << b->overflow_count()
                      << ",\"frozen_memory\":" << a->memory()
                      << ",\"e31_memory\":" << b->memory()
                      << ",\"records_compared\":" << compared
                      << ",\"dir_groups_checked\":" << dir_groups
                      << ",\"subblock_reclaims\":" << reclaims
                      << ",\"subblock_reclaim_helped\":" << reclaim_helped << "}" << std::endl;
            std::cerr << kind << " " << mode << " frac=" << frac << " ok" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

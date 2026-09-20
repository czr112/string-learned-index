// E24: update-path, space-decomposition and reclaim diagnostics.
//
// NO TIMINGS. This round answers three questions, in the order the review set
// them, and nothing else:
//
//   A. What do update() and erase() actually WALK, next to find() on the same
//      keys? The E23 audit found they linear-scan every sub-block with a full-key
//      compare while find/insert use the tag filter, the length pre-check, the
//      boundary directory and the overflow Hybrid. This measures the gap instead
//      of asserting it from the source.
//
//   B. Where has the space gone? Split into the two layers the review asked for
//      -- main-block internals, and the overflow part -- plus the separately
//      listed capacity table, model and outer objects. Capacities and live bytes
//      are reported SEPARATELY: memory_breakdown() counts capacity, and reading
//      its total as "space in use" would overstate every number here.
//
//   C. How many overflow records could LEGALLY move back into the primary? That
//      needs a free slot AND enough suffix bytes in the ONE sub-block key order
//      selects -- not "the group has free space", which is what the existing
//      group_has_room() predicate says. Both are reported so the difference is
//      visible.
//
// Test targets come from the reference map, never from the index under test.
// A probe key that find() and update() disagree about fails the run.
//
// Build with -DE24_DIAG (see tools_build.py). Nothing here is on a timing path,
// so the counters are allowed to be in the same binary as the measured calls.

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

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

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

struct Op {
    int kind;  // 0 update, 1 insert, 2 erase
    std::string key;
    std::uint64_t value;
};

// RSS is a DIFFERENT accounting basis from memory_breakdown(): the latter is the
// index's own requested bytes, this is what the process actually holds. The review
// requires them reported separately rather than reconciled into one figure.
static std::uint64_t rss_kb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<std::uint64_t>(pmc.WorkingSetSize / 1024);
    return 0;
#elif defined(__linux__)
    std::ifstream f("/proc/self/statm");
    long total = 0, resident = 0;
    if (!(f >> total >> resident)) return 0;
    return static_cast<std::uint64_t>(resident) * (sysconf(_SC_PAGESIZE) / 1024);
#else
    return 0;
#endif
}
static std::uint64_t peak_rss_kb() {
#ifdef _WIN32
    return rss_kb();
#elif defined(__linux__)
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
    return static_cast<std::uint64_t>(ru.ru_maxrss);
#else
    return 0;
#endif
}

// One index built at one state. The probe index used to derive the capacity plan
// is scoped so it is gone before anything is reported.
static std::unique_ptr<Index> build_state(const Config& cm, const std::vector<Pair>& keys,
                                          const std::vector<Op>& ops, std::size_t nops,
                                          const Config& probe_cfg) {
    Pipeline plan;
    {
        Index probe(probe_cfg);
        probe.bulk_load(keys);
        std::vector<std::vector<GroupStats>> demand;
        for (std::size_t i = 0; i < probe.region_count(); ++i)
            demand.push_back(probe.region(i).group_stats());
        plan = make_plan(demand);
    }
    auto s = std::make_unique<Index>(cm);
    s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
    for (std::size_t i = 0; i < nops; ++i) {
        const auto& o = ops[i];
        if (o.kind == 0) s->update(o.key, o.value);
        else if (o.kind == 1) s->insert(o.key, o.value);
        else s->erase(o.key);
    }
    return s;
}

int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        SetProcessAffinityMask(GetCurrentProcess(), 1);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#elif defined(__linux__)
        cpu_set_t set; CPU_ZERO(&set); CPU_SET(0, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            std::cerr << "affinity pin failed; continuing unpinned\n";
#endif
        if (argc < 3) throw std::invalid_argument("diag_e24 KIND N [TRANCOCSV] [RUNTAG]");
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
        // The query mode the E21/E22/E23 rounds used. NOT what insert() uses
        // internally -- that asymmetry is the subject of section A.
        const Mode qmode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;
        cm.reuse_group_ids = true;   // the frozen default is R1

        // ---- probe set: live keys ----------------------------------------------
        // Sampled with replacement, then DEDUPLICATED. Duplicates are fine for
        // find()/update() but not for erase(): the second erase of the same key
        // legitimately returns false, which would trip the hit-count check below
        // against a find() that found it twice.
        std::vector<std::string> probe;
        {
            std::mt19937_64 rng(2026091801);
            const std::size_t n = std::min<std::size_t>(4096, N);
            for (std::size_t i = 0; i < n; ++i) probe.push_back(keys[rng() % N].first);
            std::sort(probe.begin(), probe.end());
            probe.erase(std::unique(probe.begin(), probe.end()), probe.end());
        }
        Reference ref0;
        for (const auto& kv : keys) ref0[kv.first] = kv.second;

        // ---- probe set B: keys verified ABSENT ---------------------------------
        // The miss path is where upsert() pays twice (a full update() sweep that
        // fails, then a second lookup inside insert()). '~' appears in no synthetic
        // key and in no domain, so these are absent by construction -- but "by
        // construction" is not a check, so the reference decides.
        std::vector<std::string> probe_abs;
        {
            std::mt19937_64 rng(2026091802);
            const std::size_t n = std::min<std::size_t>(4096, N);
            for (std::size_t i = 0; i < n; ++i)
                probe_abs.push_back(keys[rng() % N].first + "~absent" + std::to_string(i));
            std::sort(probe_abs.begin(), probe_abs.end());
            probe_abs.erase(std::unique(probe_abs.begin(), probe_abs.end()), probe_abs.end());
            for (const auto& k : probe_abs)
                if (ref0.find(k) != ref0.end()) throw std::runtime_error("absent probe is present: " + k);
        }

        // ---- operation sequences, same construction as E22 ----------------------
        const std::size_t quarter = N / 4;
        std::vector<Op> upd, ins, rep;
        {
            std::vector<std::size_t> idx(N);
            for (std::size_t i = 0; i < N; ++i) idx[i] = i;
            std::mt19937_64 rng(2026091701);
            std::shuffle(idx.begin(), idx.end(), rng);
            for (std::size_t j = 0; j < quarter; ++j)
                upd.push_back({0, keys[idx[j]].first, 0x5A5A0000ULL + j});
        }
        {
            for (std::size_t j = 0; j < quarter; ++j)
                ins.push_back({1, keys[(j * 7) % N].first + "~new" + std::to_string(j), 0x1000ULL + j});
            for (const auto& o : ins)
                if (ref0.find(o.key) != ref0.end())
                    throw std::runtime_error("insert key already present: " + o.key);
        }
        {
            for (std::size_t j = 0; j < quarter; ++j) {
                rep.push_back({2, keys[(j * 3) % N].first, 0});
                rep.push_back({1, keys[(j * 3) % N].first + "~rep" + std::to_string(j), 0x2000ULL + j});
            }
        }

        struct StateDef { const char* name; const std::vector<Op>* ops; std::size_t nops; };
        const StateDef states[] = {
            {"initial",     nullptr, 0},
            {"update_only", &upd,    upd.size()},
            {"insert_only", &ins,    ins.size()},
            {"replace25",   &rep,    rep.size()},
        };

        std::ofstream out("results/main/e24_diag.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (const auto& st : states) {
            static const std::vector<Op> none;
            const std::vector<Op>& ops = st.ops ? *st.ops : none;

            // Maintain the reference OUTSIDE anything being reported.
            Reference ref = ref0;
            for (const auto& o : ops) {
                if (o.kind == 0) { auto it = ref.find(o.key); if (it == ref.end()) throw std::runtime_error("update target absent"); it->second = o.value; }
                else if (o.kind == 1) { if (ref.find(o.key) != ref.end()) throw std::runtime_error("insert target present"); ref[o.key] = o.value; }
                else { if (ref.erase(o.key) != 1) throw std::runtime_error("erase target absent"); }
            }

            auto s = build_state(cm, keys, ops, st.nops, c);
            if (s->size() != ref.size()) throw std::runtime_error("size drift vs reference");

            // ---- A. update path, SPLIT BY WHERE find() FOUND THE KEY ------------
            // The review asks for these counts split by primary hit / overflow hit /
            // miss: the three take different paths, and averaging them would hide
            // which one the write path is bad at.
            //
            // update() writes back the value the reference already holds, so the
            // state is unchanged by measuring it. erase() is destructive, so it runs
            // on a SEPARATE fresh build of the same state, which is then discarded;
            // the three buckets are erased in sequence on that one build because
            // their key sets are disjoint.
            //
            // Every answer is checked against the reference: a counter over a wrong
            // answer means nothing.
            auto& C = validation::e24::counts();

            std::vector<std::string> b_primary, b_overflow, b_miss;
            for (const auto& k : probe) {
                const Result r = s->find(k, qmode);
                const auto it = ref.find(k);
                const bool ref_has = (it != ref.end());
                if (r.found != ref_has) throw std::runtime_error("find/reference disagree: " + k);
                if (ref_has && r.value != it->second) throw std::runtime_error("find value mismatch: " + k);
                if (!r.found) b_miss.push_back(k);
                else if (r.source == 2) b_overflow.push_back(k);
                else b_primary.push_back(k);
            }
            // The synthetic absent keys should all be misses -- verified against the
            // live reference rather than trusted.
            for (const auto& k : probe_abs) {
                const Result r = s->find(k, qmode);
                if (r.found || ref.find(k) != ref.end())
                    throw std::runtime_error("absent probe is live: " + k);
                b_miss.push_back(k);
            }

            struct Bucket {
                const char* name;
                bool present;
                std::vector<std::string> keys;
                std::uint64_t find_cmps = 0, upd_cmps = 0, upd_bytes = 0,
                              upd_entries = 0, upd_blocks = 0,
                              era_cmps = 0, era_bytes = 0, era_entries = 0, era_blocks = 0;
            };
            Bucket buckets[3] = {{"primary", true, std::move(b_primary)},
                                 {"overflow", true, std::move(b_overflow)},
                                 {"miss", false, std::move(b_miss)}};

            for (auto& b : buckets) {
                for (const auto& k : b.keys) {
                    const Result r = s->find(k, qmode);
                    b.find_cmps += r.str_cmps;
                    if (r.found != b.present) throw std::runtime_error("bucket/source mismatch: " + k);
                }
                C.reset();
                std::uint64_t uh = 0;
                for (const auto& k : b.keys) {
                    const auto it = ref.find(k);
                    const std::uint64_t v = (it != ref.end()) ? it->second : 0;
                    if (s->update(k, v)) ++uh;
                }
                b.upd_cmps = C.upd_cmps; b.upd_bytes = C.upd_bytes;
                b.upd_entries = C.upd_entries; b.upd_blocks = C.upd_subblocks;
                if (uh != (b.present ? b.keys.size() : 0))
                    throw std::runtime_error("update hit count disagrees with the bucket");
            }
            {
                auto s2 = build_state(cm, keys, ops, st.nops, c);
                for (auto& b : buckets) {
                    C.reset();
                    std::uint64_t ok = 0;
                    for (const auto& k : b.keys) if (s2->erase(k)) ++ok;
                    b.era_cmps = C.era_cmps; b.era_bytes = C.era_bytes;
                    b.era_entries = C.era_entries; b.era_blocks = C.era_subblocks;
                    if (ok != (b.present ? b.keys.size() : 0))
                        throw std::runtime_error("erase hit count disagrees with the bucket");
                }
            }

            // ---- B. space, two layers --------------------------------------------
            const auto mb = s->memory_breakdown();
            std::size_t g_slots_cap = 0, g_slots_used = 0, g_suffix_cap = 0, g_suffix_live = 0,
                        g_suffix_free = 0, g_garbage = 0, g_header = 0, g_descriptor = 0;
            std::size_t ov_records = 0, ov_live_bytes = 0, ov_suffix_cap = 0, ov_container_cap = 0;
            std::size_t ov_rec_size = 0;
            std::size_t grp_ov_has_room = 0, free_slots_no_ov = 0, free_bytes_no_ov = 0;
            // ---- C. reclaim -------------------------------------------------------
            std::size_t fits_ordered = 0, fits_ordered_bytes = 0, blocked_slots = 0,
                        blocked_bytes = 0, blocked_order = 0, fits_if_any = 0;
            std::size_t groups = 0, groups_with_ov = 0;
            for (const auto& g : s->e24_report()) {
                ++groups;
                for (const auto& b : g.subs) {
                    g_slots_cap += b.slots; g_slots_used += b.used;
                    g_suffix_cap += b.suffix_cap; g_suffix_live += b.suffix_live;
                    g_suffix_free += b.suffix_free; g_garbage += b.garbage;
                    // The block ledger, term by term. Within a sub-block,
                    //   bb = sizeof(Header) + slots*sizeof(Entry) + suffix_cap
                    // so this must close exactly; the round reports whether it does.
                    g_header += sizeof(Header);
                    g_descriptor += b.slots * sizeof(Entry);
                }
                ov_records += g.ov_records; ov_live_bytes += g.ov_live_bytes;
                ov_suffix_cap += g.ov_suffix_cap; ov_container_cap += g.ov_container_cap;
                if (g.ov_rec_size) ov_rec_size = g.ov_rec_size;
                if (g.ov_records) ++groups_with_ov;
                fits_ordered += g.ov_fits_ordered;
                fits_ordered_bytes += g.ov_fits_ordered_bytes;
                blocked_slots += g.ov_blocked_slots;
                blocked_bytes += g.ov_blocked_bytes;
                blocked_order += g.ov_blocked_order;
                fits_if_any += g.ov_fits_if_any_subblock;
                // Locality of the free space: can THIS group host anything at all,
                // and separately, how much free space sits in groups that hold no
                // overflow records (i.e. free space that cannot serve them).
                std::size_t gs = 0, gb = 0;
                for (const auto& b : g.subs) { gs += b.slots - b.used; gb += b.suffix_free; }
                if (g.ov_records) {
                    if (gs && gb) ++grp_ov_has_room;
                } else {
                    free_slots_no_ov += gs; free_bytes_no_ov += gb;
                }
            }

            out << "{\"kind\":\"" << kind << "\",\"state\":\"" << st.name
                << "\",\"run_tag\":\"" << run_tag << "\""
                << ",\"keys\":" << N << ",\"live\":" << s->size()
                << ",\"regions\":" << s->region_count() << ",\"groups\":" << groups
                << ",\"probe_live\":" << probe.size()
                << ",\"probe_absent\":" << probe_abs.size()
                << ",\"rss_kb\":" << rss_kb() << ",\"peak_rss_kb\":" << peak_rss_kb();

            // A: what each path walks, split by where find() put the key.
            out << ",\"paths\":[";
            for (int i = 0; i < 3; ++i) {
                const auto& b = buckets[i];
                out << (i ? "," : "") << "{\"where\":\"" << b.name << "\",\"keys\":" << b.keys.size()
                    << ",\"find_cmps\":" << b.find_cmps
                    << ",\"upd_cmps\":" << b.upd_cmps << ",\"upd_bytes\":" << b.upd_bytes
                    << ",\"upd_entries\":" << b.upd_entries << ",\"upd_subblocks\":" << b.upd_blocks
                    << ",\"era_cmps\":" << b.era_cmps << ",\"era_bytes\":" << b.era_bytes
                    << ",\"era_entries\":" << b.era_entries << ",\"era_subblocks\":" << b.era_blocks
                    << "}";
            }
            out << "]";

            // B: the two layers, plus the ledger identity terms and the separately
            // listed metadata. `blocks` covers ONLY the block storage, so
            //   blocks == header + descriptor + suffix_cap
            // is a checkable identity, reported as main_ledger_closes.
            out << ",\"space\":{\"blocks\":" << mb.blocks << ",\"model\":" << mb.model
                << ",\"prefix\":" << mb.prefix << ",\"overflow\":" << mb.overflow
                << ",\"tables\":" << mb.tables << ",\"directory\":" << mb.directory
                << ",\"regions_self\":" << mb.regions_self << ",\"fences\":" << mb.fences
                << ",\"self\":" << mb.self << ",\"total\":" << s->memory()
                << ",\"main_header\":" << g_header << ",\"main_descriptor\":" << g_descriptor
                << ",\"main_slots_cap\":" << g_slots_cap << ",\"main_slots_used\":" << g_slots_used
                << ",\"main_suffix_cap\":" << g_suffix_cap << ",\"main_suffix_live\":" << g_suffix_live
                << ",\"main_suffix_free\":" << g_suffix_free << ",\"main_garbage\":" << g_garbage
                << ",\"main_ledger_sum\":" << (g_header + g_descriptor + g_suffix_cap)
                << ",\"ov_records\":" << ov_records << ",\"ov_live_bytes\":" << ov_live_bytes
                << ",\"ov_suffix_cap\":" << ov_suffix_cap << ",\"ov_container_cap\":" << ov_container_cap
                << ",\"ov_rec_size\":" << ov_rec_size
                << ",\"overflow_count\":" << s->overflow_count()
                << ",\"compactions\":" << s->compaction_count()
                << "}";

            // C: the legal reclaim, and WHERE the free space actually sits.
            out << ",\"reclaim\":{\"groups_with_ov\":" << groups_with_ov
                << ",\"fits_ordered\":" << fits_ordered
                << ",\"fits_ordered_bytes\":" << fits_ordered_bytes
                << ",\"blocked_slots\":" << blocked_slots
                << ",\"blocked_bytes\":" << blocked_bytes
                << ",\"blocked_order\":" << blocked_order
                << ",\"fits_if_any_subblock\":" << fits_if_any
                << ",\"grp_ov_has_slot_and_bytes\":" << grp_ov_has_room
                << ",\"free_slots_in_ovfree_groups\":" << free_slots_no_ov
                << ",\"free_bytes_in_ovfree_groups\":" << free_bytes_no_ov
                << "}"          // closes reclaim
                << "}"          // closes the record -- the outer brace
                << std::endl;

            std::cerr << kind << " " << st.name << " done (live=" << s->size()
                      << ", ov=" << ov_records << ", reclaimable=" << fits_ordered << ")" << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

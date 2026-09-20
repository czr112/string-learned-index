// E22: what happens to the frozen index under an update load?
//
// E21 established where the frozen version stands on STATIC point queries. This
// round asks how that standing moves as the index is maintained:
//
//   update_only   values only. THE CONTROL: record count, capacity tables and the
//                 overflow count must not move. If they do, the implementation or
//                 the measurement is wrong -- it is not "degradation".
//   insert_only   new keys that do not exist, spread over the key space rather than
//                 appended at the end.
//   replace       one delete + one insert per cycle, holding the live record count
//                 roughly constant so fragmentation and capacity are what change.
//
// Three rules the run obeys, because without them the numbers do not mean anything:
//
//   * Each workload starts from its OWN fresh initial state, built the same way.
//   * The timed update span contains ONLY index operations. Query verification, key
//     generation and diagnostic accounting all happen outside it.
//   * A "replace" cycle is reported as one cycle AND two operations, so a checkpoint
//     fraction is never ambiguous.
//
// The question this round answers is not "which is faster" but "when does the
// initial advantage start to erode, and in response to what". So it also records,
// at every checkpoint:
//
//   * how many regions no longer carry their initial capacity table (a rebuild
//     replaces a region with the uniform default), and the share of RECORDS they
//     hold;
//   * the share of checkpoint QUERIES that land in those regions.
//
// A few fallen-back regions can carry a lot of traffic, so counting regions alone
// would miss it.
//
// This round only OBSERVES. No maintenance policy is changed.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "native_e22.hpp"
#include "third_party/vendor/libart/src/art.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
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
#elif defined(__linux__)
#include <sched.h>
#endif

using namespace validation;
using namespace validation::e9alloc;

using Clock = std::chrono::steady_clock;
using Reference = std::map<std::string, std::uint64_t, Less>;

static double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
static volatile std::uint64_t sink = 0;
static std::uint64_t checksum(const Result& r) {
    return r.found ? r.value + 0x9e3779b97f4a7c15ULL : 0;
}

// ---------------------------------------------------------------------------
// ART's space over time, diagnostic build only. The wrapper is defined here (one
// owner, so there is no duplicate __wrap_free) and native_e22.hpp opens and closes
// its own window around construction; for the RUNNING footprint we simply read the
// live counter, which the wrappers keep current for every allocation ART makes.
//
// The timing build has none of this: per-allocation bookkeeping inside a timed span
// inflates whichever structure allocates more, and that is the mistake E16 caught.
// ---------------------------------------------------------------------------
#ifdef E20_ART_MEM
#include <malloc.h>
#include <unordered_set>

extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void* __real_aligned_alloc(std::size_t, std::size_t);
extern "C" int __real_posix_memalign(void**, std::size_t, std::size_t);
extern "C" void __real_free(void*);

namespace e22mem {
// native_e22.hpp opens and closes a window around ART construction; the running
// footprint below does not depend on it, but the symbol has to exist.
bool on = true;
long long live = 0;
long long peak = 0;
// A pointer is only subtracted on free if it was ADDED here. Without this, the
// prototype's frees (operator delete -> free) would decrement a counter their
// matching allocations never incremented, and `live` would drift negative -- the
// same class of mistake E16 found in the scope-local accounting.
inline std::unordered_set<void*>& tracked() { static std::unordered_set<void*> s; return s; }
}  // namespace e22mem

static void note(void* p) {
    if (!p) return;
    if (!e22mem::tracked().insert(p).second) return;
    e22mem::live += malloc_usable_size(p);
    if (e22mem::live > e22mem::peak) e22mem::peak = e22mem::live;
}

// Only calloc and realloc are wrapped: libart allocates its nodes through calloc,
// and the prototype does not. malloc is deliberately NOT wrapped, so the
// prototype's allocations cannot enter the count at all.
extern "C" void* __wrap_malloc(std::size_t n) { return __real_malloc(n); }
extern "C" void* __wrap_calloc(std::size_t a, std::size_t b) {
    void* p = __real_calloc(a, b);
    note(p);
    return p;
}
extern "C" void* __wrap_realloc(void* q, std::size_t n) {
    if (e22mem::tracked().erase(q)) e22mem::live -= (long long)malloc_usable_size(q);
    void* p = __real_realloc(q, n);
    note(p);
    return p;
}
extern "C" void* __wrap_aligned_alloc(std::size_t a, std::size_t n) { return __real_aligned_alloc(a, n); }
extern "C" int __wrap_posix_memalign(void** o, std::size_t a, std::size_t n) { return __real_posix_memalign(o, a, n); }
extern "C" void __wrap_free(void* p) {
    if (p && e22mem::tracked().erase(p)) e22mem::live -= (long long)malloc_usable_size(p);
    __real_free(p);
}
#endif

struct Pipeline {
    std::vector<Alloc> d_alloc, s_alloc;
    std::vector<std::vector<std::uint16_t>> s_caps;
};
static Pipeline make_plan(const std::vector<std::vector<GroupStats>>& demand) {
    Pipeline p;
    for (const auto& gs : demand) {
        const std::uint64_t budget = std::uint64_t(gs.size()) * 1024;
        p.d_alloc.push_back(demand_single(gs, budget));
        StrictLog log;
        std::vector<std::uint16_t> caps;
        p.s_alloc.push_back(demand_strict(gs, p.d_alloc.back(), log, caps));
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

// One index operation. kind: 0 update, 1 insert, 2 erase.
struct Op {
    int kind = 0;
    std::string key;
    std::uint64_t value = 0;
};

struct RegionShape {
    std::vector<std::uint16_t> sg;
    std::vector<std::uint32_t> bb;
};

static std::vector<RegionShape> snapshot_shapes(const Index& idx) {
    std::vector<RegionShape> out;
    for (std::size_t i = 0; i < idx.region_count(); ++i)
        out.push_back({idx.region(i).slot_table(), idx.region(i).block_table()});
    return out;
}

static bool same_shape(const RegionShape& a, const Index& idx, std::size_t i) {
    return a.sg == idx.region(i).slot_table() && a.bb == idx.region(i).block_table();
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
        if (argc < 3) throw std::invalid_argument("bench_e22 KIND N [TRANCOCSV] [RUNTAG]");
        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";
        const std::string run_tag = argc > 4 ? argv[4] : "";
        const auto keys = make_data(kind, nkeys, trcsv);
        const std::size_t N = keys.size();

        const std::string frozen_rel = "../validation_e7_20260913/results/main/frozen_ov_" + kind + ".json";
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
        const Mode mode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, thr, false};

        Config c;
        c.block_bytes = 1024;
        c.slots = 16;
        c.model_kind = ModelKind::Context16;
        Config cm = c;
        cm.multiblock = true;
        cm.reuse_group_ids = true;   // the frozen default is R1

        // A verified-absent test: the reference is the authority for "live".
        auto absent = [](const Reference& ref, const std::string& k) {
            return ref.find(k) == ref.end();
        };

        // ---- the three operation sequences, built once and reused --------------
        // Budget is 25% of the initial key count in every workload; checkpoints are
        // taken at 0/5/10/25% of it.
        const std::size_t quarter = N / 4;
        std::vector<std::size_t> marks;
        for (double f : {0.0, 0.05, 0.10, 0.25})
            marks.push_back(static_cast<std::size_t>(double(N) * f));

        std::vector<Op> upd, ins, rep;
        {
            std::vector<std::size_t> idx(N);
            for (std::size_t i = 0; i < N; ++i) idx[i] = i;
            std::mt19937_64 rng(2026091701);
            std::shuffle(idx.begin(), idx.end(), rng);
            // control: every key exists, only its value changes
            for (std::size_t j = 0; j < quarter; ++j)
                upd.push_back({0, keys[idx[j]].first, 0x5A5A0000ULL + j});
        }
        {
            for (std::size_t j = 0; j < quarter; ++j) {
                // Anchored on a real key, so inserts spread over the key space
                // instead of piling up at one end.
                const std::string k = keys[(j * 7) % N].first + "~new" + std::to_string(j);
                ins.push_back({1, k, 0x1000ULL + j});
            }
            // The synthetic sets are built from letters, digits and '/'; Tranco keys
            // are domains. Neither contains '~', so the anchors are absent by
            // construction -- but "by construction" is not a check, so verify.
            Reference probe_ref;
            for (const auto& kv : keys) probe_ref[kv.first] = kv.second;
            for (const auto& o : ins)
                if (!absent(probe_ref, o.key))
                    throw std::runtime_error("insert key already present: " + o.key);
        }
        {
            for (std::size_t j = 0; j < quarter; ++j) {
                // Distinct erase targets: 3j mod N is injective for these N.
                rep.push_back({2, keys[(j * 3) % N].first, 0});
                rep.push_back({1, keys[(j * 3) % N].first + "~rep" + std::to_string(j), 0x2000ULL + j});
            }
        }

        // ---- query sets ---------------------------------------------------------
        // A: a FIXED key sequence. The same access demand every checkpoint; the true
        //    answer is recomputed from the live state, and the actual hit rate is
        //    reported because it will fall as keys are replaced.
        std::vector<std::string> qA;
        {
            std::mt19937_64 rng(2026091702);
            const std::size_t n = std::min<std::size_t>(65536, N);
            for (std::size_t i = 0; i < n; ++i) qA.push_back(keys[rng() % N].first);
        }

        std::ofstream out("results/main/e22_dynamic.jsonl", std::ios::app);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(10);

        for (const char* wl : {"update_only", "insert_only", "replace"}) {
            const std::string workload = wl;
            const std::vector<Op>& ops = workload == "update_only" ? upd
                                      : workload == "insert_only" ? ins : rep;
            const bool cycles = (workload == "replace");

            // ---- fresh initial state for THIS workload --------------------------
            Reference ref;
            for (const auto& kv : keys) ref[kv.first] = kv.second;

            Pipeline plan;
            {
                // Scoped: the probe is not part of the measured state, and it must be
                // gone before any metric is taken so it cannot inflate the space
                // figure.
                Index probe(c);
                probe.bulk_load(keys);
                std::vector<std::vector<GroupStats>> demand;
                for (std::size_t i = 0; i < probe.region_count(); ++i)
                    demand.push_back(probe.region(i).group_stats());
                plan = make_plan(demand);
            }

            auto s = std::make_unique<Index>(cm);
            s->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
            const auto initial_shapes = snapshot_shapes(*s);
            const std::size_t initial_regions = s->region_count();

            auto art = std::make_unique<e22art::NativeArt>(keys);

            // ---- checkpoints ----------------------------------------------------
            std::size_t consumed = 0;   // operations (or cycles) applied so far
            std::size_t applied = 0;    // index operations
            double update_ms = 0.0, update_art_ms = 0.0;
            std::size_t step = 0;

            for (std::size_t mark : marks) {
                // ---- apply up to `mark` cycles/ops, TIMED ------------------------
                if (mark > consumed) {
                    const std::size_t target = std::min<std::size_t>(mark, cycles ? ops.size() / 2 : ops.size());
                    std::vector<Op> seg;
                    const std::size_t end = cycles ? target * 2 : target;
                    for (std::size_t i = (cycles ? consumed * 2 : consumed); i < end; ++i)
                        seg.push_back(ops[i]);

                    // Reference first, OUTSIDE the timed span: maintaining it is not
                    // part of what is being measured.
                    for (const auto& o : seg) {
                        if (o.kind == 0) {
                            auto it = ref.find(o.key);
                            if (it == ref.end()) throw std::runtime_error("update target absent");
                            it->second = o.value;
                        } else if (o.kind == 1) {
                            if (!absent(ref, o.key)) throw std::runtime_error("insert target present");
                            ref[o.key] = o.value;
                        } else {
                            if (ref.erase(o.key) != 1) throw std::runtime_error("erase target absent");
                        }
                    }

                    // Alternate which index goes first, so "ran second" is not
                    // systematically the same structure.
                    const bool s_first = (step % 2) == 0;
                    double t_s = 0, t_art = 0;
                    auto run_s = [&] {
                        const auto a = Clock::now();
                        for (const auto& o : seg) {
                            if (o.kind == 0) s->update(o.key, o.value);
                            else if (o.kind == 1) s->insert(o.key, o.value);
                            else s->erase(o.key);
                        }
                        t_s = ms_between(a, Clock::now());
                    };
                    auto run_art = [&] {
                        const auto a = Clock::now();
                        for (const auto& o : seg) {
                            if (o.kind == 0 || o.kind == 1) art->upsert(o.key, o.value);
                            else art->erase(o.key);
                        }
                        t_art = ms_between(a, Clock::now());
                    };
                    if (s_first) { run_s(); run_art(); } else { run_art(); run_s(); }
                    // BOTH update costs are reported. Which one ran first is
                    // alternated, so the order cannot be read as a difference.
                    update_ms = t_s;
                    update_art_ms = t_art;
                    consumed = target;
                    applied += seg.size();
                    if (s->size() != ref.size()) throw std::runtime_error("size drift after update");
                }

                // ---- metrics ------------------------------------------------------
                std::size_t fallback_regions = 0, fallback_records = 0, total_records = 0;
                std::size_t compactions = 0;
                for (std::size_t i = 0; i < s->region_count(); ++i) {
                    total_records += s->region(i).size();
                    compactions += s->region(i).compaction_count();
                    if (i < initial_shapes.size() && !same_shape(initial_shapes[i], *s, i)) {
                        ++fallback_regions;
                        fallback_records += s->region(i).size();
                    } else if (i >= initial_shapes.size()) {
                        ++fallback_regions;
                        fallback_records += s->region(i).size();
                    }
                }
                std::size_t fallback_qA = 0;
                for (const auto& k : qA) {
                    const std::size_t r = s->locate(k);
                    if (r >= initial_shapes.size() || !same_shape(initial_shapes[r], *s, r))
                        ++fallback_qA;
                }

                // ---- query set A: fixed sequence, answers from the live state ------
                std::size_t hitA = 0;
                double nsA_s = 0, nsA_art = 0;
                {
                    std::uint64_t want = 0;
                    for (const auto& k : qA) {
                        auto it = ref.find(k);
                        if (it != ref.end()) { ++hitA; want += it->second + 0x9e3779b97f4a7c15ULL; }
                    }
                    for (const auto& k : qA) {   // verify, outside the timed span
                        const bool live = ref.find(k) != ref.end();
                        const auto rs = s->find(k, mode);
                        const auto ra = art->find(k);
                        if (rs.found != live || ra.found != live)
                            throw std::runtime_error("query set A: found mismatch");
                        if (live && (rs.value != ref.find(k)->second || ra.value != ref.find(k)->second))
                            throw std::runtime_error("query set A: value mismatch");
                    }
                    for (std::size_t i = 0; i < std::min<std::size_t>(20000, qA.size()); ++i)
                        sink = checksum(s->find(qA[i], mode));
                    std::uint64_t sum = 0;
                    auto a0 = Clock::now();
                    for (const auto& k : qA) sum += checksum(s->find(k, mode));
                    auto a1 = Clock::now();
                    if (sum != want) throw std::runtime_error("set A checksum (prototype)");
                    nsA_s = std::chrono::duration<double, std::nano>(a1 - a0).count() / double(qA.size());
                    sum = 0;
                    a0 = Clock::now();
                    for (const auto& k : qA) sum += checksum(art->find(k));
                    a1 = Clock::now();
                    if (sum != want) throw std::runtime_error("set A checksum (ART)");
                    nsA_art = std::chrono::duration<double, std::nano>(a1 - a0).count() / double(qA.size());
                }

                // ---- query set B: regenerated to a fixed 50% hit ratio -------------
                std::vector<std::string> qB;
                std::size_t hitB = 0;
                double nsB_s = 0, nsB_art = 0;
                {
                    std::mt19937_64 rng(2026091703 + step);
                    std::vector<std::string> live;
                    live.reserve(ref.size());
                    for (const auto& kv : ref) live.push_back(kv.first);
                    const std::size_t n = 32768;
                    for (std::size_t i = 0; i < n; ++i) qB.push_back(live[rng() % live.size()]);
                    for (std::size_t i = 0; i < n; ++i) {
                        std::string k;
                        std::size_t guard = 0;
                        do {
                            k = keys[rng() % N].first + "~q" + std::to_string(step) + "_" + std::to_string(i) +
                                "_" + std::to_string(guard++);
                        } while (!absent(ref, k) && guard < 8);
                        if (!absent(ref, k)) throw std::runtime_error("could not build an absent key for set B");
                        qB.push_back(k);
                    }
                    std::shuffle(qB.begin(), qB.end(), rng);
                    hitB = n;
                    std::uint64_t want = 0;
                    for (const auto& k : qB) {
                        auto it = ref.find(k);
                        if (it != ref.end()) want += it->second + 0x9e3779b97f4a7c15ULL;
                    }
                    for (const auto& k : qB) {
                        const bool lv = ref.find(k) != ref.end();
                        if (s->find(k, mode).found != lv || art->find(k).found != lv)
                            throw std::runtime_error("query set B: found mismatch");
                    }
                    for (std::size_t i = 0; i < std::min<std::size_t>(20000, qB.size()); ++i)
                        sink = checksum(s->find(qB[i], mode));
                    std::uint64_t sum = 0;
                    auto a0 = Clock::now();
                    for (const auto& k : qB) sum += checksum(s->find(k, mode));
                    auto a1 = Clock::now();
                    if (sum != want) throw std::runtime_error("set B checksum (prototype)");
                    nsB_s = std::chrono::duration<double, std::nano>(a1 - a0).count() / double(qB.size());
                    sum = 0;
                    a0 = Clock::now();
                    for (const auto& k : qB) sum += checksum(art->find(k));
                    a1 = Clock::now();
                    if (sum != want) throw std::runtime_error("set B checksum (ART)");
                    nsB_art = std::chrono::duration<double, std::nano>(a1 - a0).count() / double(qB.size());
                }

                // ---- record ---------------------------------------------------------
                out << "{\"kind\":\"" << kind << "\",\"workload\":\"" << workload
                    << "\",\"run_tag\":\"" << run_tag << "\""
                    << ",\"checkpoint\":{\"cycles\":" << (cycles ? consumed : 0)
                    << ",\"ops\":" << applied
                    << ",\"fraction_of_initial_keys\":" << (double(consumed) / double(N)) << "}"
                    << ",\"records\":" << s->size() << ",\"reference_records\":" << ref.size()
                    << ",\"regions\":" << s->region_count() << ",\"initial_regions\":" << initial_regions
                    << ",\"overflow_records\":" << s->overflow_count()
                    << ",\"rebuilds\":" << s->rebuilds << ",\"splits\":" << s->splits
                    << ",\"compactions\":" << compactions
                    << ",\"space_prototype\":" << s->memory()
                    << ",\"mem_build\":" <<
#ifdef E20_ART_MEM
                       "true"
#else
                       "false"
#endif
                    << ",\"space_art_live\":" <<
#ifdef E20_ART_MEM
                       static_cast<long long>(e22mem::live)
#else
                       0LL
#endif
                    << ",\"update_ms\":" << update_ms
                    << ",\"update_ms_art\":" << update_art_ms
                    << ",\"fallback\":{\"regions\":" << fallback_regions
                    << ",\"record_share\":" << (total_records ? double(fallback_records) / double(total_records) : 0.0)
                    << ",\"query_share_setA\":" << (qA.empty() ? 0.0 : double(fallback_qA) / double(qA.size()))
                    << "}"
                    << ",\"queryA\":{\"n\":" << qA.size() << ",\"hits\":" << hitA
                    << ",\"ns_prototype\":" << nsA_s << ",\"ns_art\":" << nsA_art << "}"
                    << ",\"queryB\":{\"n\":" << qB.size() << ",\"hits\":" << hitB
                    << ",\"ns_prototype\":" << nsB_s << ",\"ns_art\":" << nsB_art << "}"
                    << "}" << std::endl;
                ++step;
            }
            std::cerr << "workload " << workload << " complete for " << kind << std::endl;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

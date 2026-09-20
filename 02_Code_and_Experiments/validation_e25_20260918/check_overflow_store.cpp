// E34/O1: the differential gate for the stable overflow store (Config::overflow_stable_store).
//
// WHAT O1 IS. The ordered overflow container is a vector of 48-byte records, so every erase
// shifts every later record -- E32 measured the container at 12.2%/37.3%/37.6% of maintenance
// time, with the cost concentrated in ERASE. O1 replaces that with STABLE record storage plus
// an ORDERED HANDLE array: records stop moving, and what moves is a 4-byte handle.
//
// WHAT THIS GATE IS NOT. It is a CORRECTNESS gate and says NOTHING about speed. O1 is not
// assumed to be faster: it still moves handles, and it ADDS an indirection on every read, so
// the point-query, scan and insert paths may get SLOWER. That is exactly what the timing
// round is for. What is established here is only that the two representations hold the same
// records, in the same order, through the same operations.
//
// WHAT IT ESTABLISHES, on mixed/skew (131072) and tranco (262144):
//   1. two indexes from ONE plan. A = the frozen configuration, B = frozen plus the new
//      switch. require_only_switches_differ asserts the arms differ in exactly that switch,
//      so an arm that silently became a copy of the other is a failure and not a pass.
//   2. one operation sequence through both, heavy on the overflow: inserts that overrun many
//      groups, erases of records that ARE in overflow, then re-inserts into the same groups.
//   3. after every batch, full content AND key order identical between the arms and against an
//      independent std::map reference, element by element, VALUES INCLUDED.
//   4. point queries: every reference key, plus keys absent from it, must get the same answer
//      on both arms. This covers the Hybrid overflow search, the path most exposed to a change
//      in the container's layout.
//   5. full scans and arbitrary-start scans at counts 1/2/16, element by element, both arms.
//   6. NON-VACUITY, in two independent pieces, because a gate that passes without exercising
//      the code under test certifies nothing -- the failure this project has shipped once:
//        a. the erases must really have hit the overflow container. The victim keys are
//           SELECTED by asking the query path where they live (Result::source == 2 means the
//           overflow container answered), and the arm-A delta of erase_released_overflow is
//           asserted positive. A run whose erases all landed in the primary fails here.
//        b. arm B must really have used the new representation. The container reports what it
//           PHYSICALLY moved and the gate reads it: overflow_handle_moves (handles the order
//           array shifted) must be positive, overflow_record_moves_stable (stored records
//           relocated) must be zero, and the store's live high-water mark must be positive.
//           Those counters are written inside the container and only on the stable arm, so a
//           switch that never took effect cannot produce them.
//
// WHAT IT DOES NOT COVER. Speed (above). The container's internal layout: the gate compares
// what the index HOLDS, never how it stores it, so an implementation with the wrong handle
// ORDER is caught here while one that is merely slow is not. Concurrency: the index is not
// thread-safe and this gate is single-threaded. And the memory trade: O1 swaps record
// movement for a handle array plus a free list and is expected to use MORE memory at rest;
// nothing here asserts on that, because it is a size question and not a correctness one.

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef E25_DIAG
#error "check_overflow_store must be built with -DE25_DIAG: its non-vacuity assertions read the branch counters"
#endif

using namespace validation;
using namespace validation::e9alloc;

using Reference = std::map<std::string, std::uint64_t, Less>;

// ---------------------------------------------------------------------------
// The build plan. One plan for both arms: the demand pass is a property of the data and not
// of the representation under test, so a second pass would compare two different structures.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Read-only addressing, used to aim the forced-overflow inserts at real groups.
// ---------------------------------------------------------------------------

struct GroupRef {
    std::size_t region;
    std::uint32_t group;
};

static std::size_t group_entries(const Index& idx, const GroupRef& ref) {
    const Region& reg = idx.region(ref.region);
    std::size_t n = 0;
    for (std::size_t i = 0, nb = reg.subblock_count(ref.group); i < nb; ++i)
        n += reg.subblock_entries(ref.group, i);
    return n;
}

static std::vector<GroupRef> live_groups(const Index& idx) {
    std::vector<GroupRef> out;
    for (std::size_t r = 0; r < idx.region_count(); ++r) {
        const Region& reg = idx.region(r);
        for (std::uint32_t g = 0; g < reg.groups(); ++g) {
            const GroupRef ref{r, g};
            if (group_entries(idx, ref)) out.push_back(ref);
        }
    }
    return out;
}

static std::string group_max_key(const Index& idx, const GroupRef& ref) {
    const Region& reg = idx.region(ref.region);
    for (std::size_t i = reg.subblock_count(ref.group); i > 0; --i) {
        if (reg.subblock_entries(ref.group, i - 1) == 0) continue;
        const auto keys = reg.subblock_keys(ref.group, i - 1);
        return reg.prefix() + keys.back();
    }
    throw std::runtime_error("the group has no records to aim an insert after");
}

// ---------------------------------------------------------------------------
// NON-VACUITY, part 1: the counters that say the new representation was used.
//
// The implementation patch is o1_overflow_stable_store.py in this same patch directory. Its
// container (Region::OverflowList) exposes the counters below, and these names are taken from
// it verbatim. They are incremented INSIDE the container and ONLY while the stable arm is
// live: the frozen arm's containers are the plain vector and never touch them, so their values
// at the end of this run are arm B's alone. A switch that never took effect cannot produce
// them, which is what makes them a non-vacuity signal rather than decoration.
//
//   overflow_handle_moves          handles the order array shifted (stable arm only)
//   overflow_record_moves_stable   stored records relocated (stable arm only; the store never
//                                  relocates one, and the count sits on the only line that
//                                  touches the store, so a non-zero value is a real finding)
//   ov_store_live_water            peak live count held by the store (stable arm only)
//
// The E32 counters the earlier rounds used -- ov_shifted_records_erase and
// ov_shifted_records_insert -- are NOT usable here. The implementation keeps them as the
// LOGICAL shift distance in BOTH arms on purpose, so the two arms are compared on the same
// workload and only the byte figures move. Expecting the stable arm to report zero there would
// fail a correct implementation, so this gate does not expect it. It asserts instead that the
// arms report the SAME logical distance, which is the implementation's stated contract and a
// direct check that the two arms really did see the same workload.
// ---------------------------------------------------------------------------

using O1Counts = e25::Counts;

// ---------------------------------------------------------------------------
// The operation sequence. Every step is applied to BOTH arms and to the reference, so a
// disagreement is attributed to the representation and not to a divergence in the workload.
// ---------------------------------------------------------------------------

struct Step {
    int kind;              // 1 = insert, 2 = erase
    std::string key;
    std::uint64_t value;
};

static void fold_reference(Reference& ref, const std::vector<Step>& steps,
                           std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        const Step& s = steps[i];
        if (s.kind == 1) {
            if (ref.count(s.key))
                throw std::runtime_error("the plan inserts a key that is already present: " + s.key);
            ref[s.key] = s.value;
        } else {
            if (ref.erase(s.key) != 1)
                throw std::runtime_error("the plan erases a key that is not present: " + s.key);
        }
    }
}

static void apply_steps(Index& idx, const std::vector<Step>& steps,
                        std::size_t from, std::size_t to, const char* arm) {
    for (std::size_t i = from; i < to; ++i) {
        const Step& s = steps[i];
        if (s.kind == 1) {
            if (!idx.insert(s.key, s.value))
                throw std::runtime_error(std::string(arm) + " refused an insert of a fresh key: " + s.key);
        } else {
            if (!idx.erase(s.key))
                throw std::runtime_error(std::string(arm) + " could not erase a live key: " + s.key);
        }
    }
}

// The forced-overflow inserts. A key is the group's own maximum with a byte appended that no
// generated key carries, so it is provably fresh and it sorts immediately after that key,
// which keeps it in the same group. The run length is the group's CURRENT occupancy plus the
// Hybrid cutoff plus a margin: a group cannot hold twice its planned occupancy in the primary,
// so the tail of the run is forced into the overflow container.
static std::vector<Step> forced_overflow_steps(const Index& idx, std::uint32_t thr,
                                               std::unordered_set<std::string>& used) {
    const auto refs = live_groups(idx);
    if (refs.empty()) throw std::runtime_error("the index has no live groups");

    std::vector<Step> steps;
    const std::size_t want = std::min<std::size_t>(48, refs.size());

    for (std::size_t j = 0; j < want; ++j) {
        const GroupRef gr = refs[j * refs.size() / want];
        const std::size_t count = group_entries(idx, gr) + thr + 64;
        const std::string base = group_max_key(idx, gr);

        for (std::size_t k = 0; k < count; ++k) {
            std::string fresh = base;
            fresh.push_back('\x02');
            fresh += std::to_string(k);
            if (!used.insert(fresh).second)
                throw std::runtime_error("the forced-overflow key was not fresh: " + fresh);
            steps.push_back(Step{1, fresh, 0xA1000000ULL + steps.size()});
        }
    }

    return steps;
}

// ---------------------------------------------------------------------------
// Element-by-element comparisons. A checksum would let two different indexes agree.
// ---------------------------------------------------------------------------

static void assert_same_content(const Index& a, const Index& b, const Reference& ref,
                                const std::string& phase, std::uint64_t& compared) {
    const auto ca = a.scan(std::string_view{}, a.size());
    const auto cb = b.scan(std::string_view{}, b.size());

    if (ca.size() != ref.size())
        throw std::runtime_error(phase + ": the frozen arm holds " + std::to_string(ca.size()) +
                                 " records, the reference holds " + std::to_string(ref.size()));

    if (cb.size() != ref.size())
        throw std::runtime_error(phase + ": the stable-store arm holds " + std::to_string(cb.size()) +
                                 " records, the reference holds " + std::to_string(ref.size()));

    // The reference iterates in key order, so comparing position by position checks the KEY
    // ORDER as well as the content. Reaching the end of the map is checked, so a scan that
    // returned the right records in the wrong order cannot pass.
    auto want = ref.begin();
    for (std::size_t i = 0; i < ca.size(); ++i, ++want) {
        ++compared;
        if (want == ref.end())
            throw std::runtime_error(phase + ": the scan returned more records than the reference");
        if (ca[i].first != want->first || ca[i].second != want->second)
            throw std::runtime_error(phase + ": the frozen arm disagrees with the reference at " +
                                     std::to_string(i) + " key=" + ca[i].first);
        if (cb[i].first != ca[i].first || cb[i].second != ca[i].second)
            throw std::runtime_error(phase + ": the arms differ at " + std::to_string(i) +
                                     " key=" + ca[i].first);
    }
    if (want != ref.end())
        throw std::runtime_error(phase + ": the scan returned fewer records than the reference");
}

static void assert_find_agrees(const Index& a, const Index& b, const std::string& key,
                               const Mode& mode, bool expect_found, std::uint64_t expect_value,
                               std::uint64_t& checked) {
    const Result ra = a.find(key, mode);
    const Result rb = b.find(key, mode);
    ++checked;

    if (ra.found != rb.found)
        throw std::runtime_error("the arms answer find() differently for " + key);

    if (expect_found) {
        if (!ra.found)
            throw std::runtime_error("a live key is not found: " + key);
        if (ra.value != expect_value || rb.value != expect_value)
            throw std::runtime_error("a live key is answered with the wrong value: " + key);
    } else {
        if (ra.found)
            throw std::runtime_error("an absent key is answered as present: " + key);
    }
}

static void assert_scan_agrees(const Index& a, const Index& b, const Reference& ref,
                               const std::string& start, std::size_t count,
                               std::uint64_t& scans, std::uint64_t& records) {
    const auto sa = a.scan(start, count);
    const auto sb = b.scan(start, count);

    if (sa.size() != sb.size())
        throw std::runtime_error("the arms scan different lengths from start=" + start);

    auto want = ref.lower_bound(start);
    for (std::size_t i = 0; i < sa.size(); ++i, ++want) {
        ++records;
        if (want == ref.end())
            throw std::runtime_error("the scan returned more records than the reference holds");
        if (sa[i].first != want->first || sa[i].second != want->second)
            throw std::runtime_error("the frozen arm's scan disagrees with the reference at " +
                                     std::to_string(i) + " key=" + sa[i].first);
        if (sb[i].first != sa[i].first || sb[i].second != sa[i].second)
            throw std::runtime_error("the arms scan differently at " + std::to_string(i) +
                                     " key=" + sa[i].first);
    }
    ++scans;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        if (argc < 3)
            throw std::invalid_argument("check_overflow_store KIND N [TRANCOCSV] [RUNTAG]");

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
            std::stringstream ss;
            ss << fz.rdbuf();
            const std::string text = ss.str();
            const auto position = text.find("\"frozen_hybrid_threshold\":");
            if (position == std::string::npos) throw std::runtime_error("frozen field missing");
            thr = static_cast<std::uint32_t>(std::stoul(text.substr(position + 26)));
        }

        // Arm A, the frozen configuration, stated field by field rather than inherited from a
        // Config default -- the same five switches every other gate pins.
        Config frozen = e25sw::baseline_config(thr);
        frozen.write_fast_locate = true;
        frozen.reuse_locate_ctx = true;
        frozen.compact_reuse_tag = true;
        frozen.compact_needs_garbage = true;
        frozen.scan_reserve_result = true;

        Config stable = frozen;
        stable.overflow_stable_store = true;

        // The switch name here is the one the brief names for the case where the
        // implementation patch is not yet on disk: overflow_stable_store.
        e25sw::require_only_switches_differ(frozen, stable, {"overflow_stable_store"});

        // require_only_switches_differ can only check the switches its hand-maintained table
        // lists. A switch that was never added to that table makes the assertion above pass
        // VACUOUSLY -- this project has already shipped that bug once. The two arms are
        // therefore also compared on the one field this gate is about, directly.
        if (frozen.overflow_stable_store || !stable.overflow_stable_store)
            throw std::runtime_error("the arms do not differ in overflow_stable_store: "
                                     "every comparison below would be between identical configurations");

        std::cerr << "check_overflow_store frozen[" << e25sw::switches(frozen) << "] O1["
                  << e25sw::switches(stable) << "]" << std::endl;

        const Mode qmode = Mode{frozen.canonical, Prefetch::Off, true, true,
                                OvSearch::Hybrid, frozen.ov_threshold};

        Pipeline plan;
        Index probe(frozen);
        probe.bulk_load(keys);
        {
            std::vector<std::vector<GroupStats>> demand;
            for (std::size_t i = 0; i < probe.region_count(); ++i)
                demand.push_back(probe.region(i).group_stats());
            plan = make_plan(demand);
        }

        Reference ref;
        for (const auto& kv : keys) ref[kv.first] = kv.second;

        auto a = std::make_unique<Index>(frozen);
        auto b = std::make_unique<Index>(stable);
        a->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);
        b->bulk_load_with_plan(keys, sg_of(plan.s_alloc), bb_of(plan.s_alloc), plan.s_caps);

        std::uint64_t compared = 0;
        std::uint64_t finds = 0;
        std::uint64_t scans = 0;
        std::uint64_t scan_records = 0;

        auto checkpoint = [&](const std::string& name) {
            if (a->size() != ref.size())
                throw std::runtime_error(name + ": the frozen arm's size != the reference's");
            if (b->size() != ref.size())
                throw std::runtime_error(name + ": the stable-store arm's size != the reference's");
            assert_same_content(*a, *b, ref, name, compared);
            std::cout << "{\"phase\":\"checkpoint\",\"kind\":\"" << kind << "\",\"name\":\""
                      << name << "\",\"live\":" << ref.size()
                      << ",\"arm_a_overflow\":" << a->overflow_count()
                      << ",\"arm_b_overflow\":" << b->overflow_count()
                      << ",\"records_compared\":" << compared << "}" << std::endl;
        };

        checkpoint("initial");

        // ---- Batch 1: force overflow -------------------------------------------
        std::unordered_set<std::string> used;
        for (const auto& kv : keys) used.insert(kv.first);

        const std::vector<Step> forced = forced_overflow_steps(probe, thr, used);
        fold_reference(ref, forced, 0, forced.size());

        const O1Counts ins_before_a = e25::counts();
        apply_steps(*a, forced, 0, forced.size(), "arm A");
        const O1Counts ins_after_a = e25::counts();
        const O1Counts ins_before_b = e25::counts();
        apply_steps(*b, forced, 0, forced.size(), "arm B");
        const O1Counts ins_after_b = e25::counts();

        std::cout << "{\"phase\":\"batch\",\"name\":\"overflow_insert\""
                  << ",\"steps\":" << forced.size()
                  << ",\"arm_a_overflow\":" << a->overflow_count()
                  << ",\"arm_b_overflow\":" << b->overflow_count()
                  << ",\"arm_a_record_shifts\":" << (ins_after_a.ov_shifted_records_insert - ins_before_a.ov_shifted_records_insert)
                  << ",\"arm_b_record_shifts\":" << (ins_after_b.ov_shifted_records_insert - ins_before_b.ov_shifted_records_insert)
                  << ",\"arm_b_handle_moves\":" << (ins_after_b.overflow_handle_moves - ins_before_b.overflow_handle_moves)
                  << "}" << std::endl;

        checkpoint("overflow_insert");

        // ---- Victim selection: which inserted records actually live in overflow --
        // The query path answers this directly: Result::source == 2 means the overflow
        // container produced the hit. The selection is made on arm A and re-checked on arm B,
        // so a representation that files a record in a DIFFERENT container than its twin
        // cannot pass as "both arms erased something".
        std::vector<std::string> victims;
        for (const auto& s : forced) {
            const Result ra = a->find(s.key, qmode);
            if (!ra.found)
                throw std::runtime_error("a key the gate just inserted is not found: " + s.key);
            if (ra.source != 2) continue;
            victims.push_back(s.key);
        }

        if (victims.empty())
            throw std::runtime_error("the forced inserts put nothing in the overflow container: "
                                     "the erase half of this gate would test nothing");

        for (const auto& v : victims) {
            const Result rb = b->find(v, qmode);
            if (!rb.found || rb.source != 2)
                throw std::runtime_error("the arms disagree about where a record lives: " + v);
        }

        std::cout << "{\"phase\":\"victims\",\"kind\":\"" << kind << "\""
                  << ",\"overflow_resident\":" << victims.size() << "}" << std::endl;

        // Erase a bounded, evenly spread sample. Spreading them matters: a record erased at
        // the END of a container shifts nothing, so a sample that happened to take only the
        // last slot of each container could make the frozen arm's movement look small for a
        // reason that has nothing to do with the representation.
        std::vector<Step> erase_steps;
        {
            const std::size_t want = 512;
            const std::size_t stride = std::max<std::size_t>(1, victims.size() / want);
            for (std::size_t i = 0; i < victims.size() && erase_steps.size() < want; i += stride)
                erase_steps.push_back(Step{2, victims[i], 0});
        }

        fold_reference(ref, erase_steps, 0, erase_steps.size());

        const O1Counts era_before_a = e25::counts();
        apply_steps(*a, erase_steps, 0, erase_steps.size(), "arm A");
        const O1Counts era_after_a = e25::counts();
        const O1Counts era_before_b = e25::counts();
        apply_steps(*b, erase_steps, 0, erase_steps.size(), "arm B");
        const O1Counts era_after_b = e25::counts();

        const std::uint64_t a_overflow_released =
            era_after_a.erase_released_overflow - era_before_a.erase_released_overflow;
        const std::uint64_t a_logical_shifts =
            era_after_a.ov_shifted_records_erase - era_before_a.ov_shifted_records_erase;
        const std::uint64_t b_logical_shifts =
            era_after_b.ov_shifted_records_erase - era_before_b.ov_shifted_records_erase;
        const std::uint64_t b_handle_moves =
            era_after_b.overflow_handle_moves - era_before_b.overflow_handle_moves;
        const std::uint64_t b_record_moves =
            era_after_b.overflow_record_moves_stable - era_before_b.overflow_record_moves_stable;

        // NON-VACUITY, part 2a: the erases reached the container under test.
        if (a_overflow_released == 0)
            throw std::runtime_error("not one erase reached the overflow container on the frozen arm: "
                                     "the container under test was never touched");

        if (a_logical_shifts == 0)
            throw std::runtime_error("the frozen arm's erases shifted nothing: the container was not "
                                     "in the state this gate claims to exercise");

        // The two arms must have seen the SAME workload. The implementation keeps
        // ov_shifted_records_erase as the logical shift distance in both arms, so a difference
        // here means the arms diverged earlier and nothing below is a controlled comparison.
        if (a_logical_shifts != b_logical_shifts)
            throw std::runtime_error("the arms report different logical shift distances (" +
                                     std::to_string(a_logical_shifts) + " vs " +
                                     std::to_string(b_logical_shifts) +
                                     "): the two arms did not see the same workload");

        // NON-VACUITY, part 2b: arm B used the new representation. These counters live inside
        // the container and only the stable arm writes them, so a switch that never took effect
        // cannot produce them.
        if (b_handle_moves == 0)
            throw std::runtime_error("the stable-store arm shifted no handles during erase: the switch "
                                     "did not take effect, so every comparison in this gate was between "
                                     "two identical representations");

        if (b_record_moves != 0)
            throw std::runtime_error("the stable-store arm relocated " + std::to_string(b_record_moves) +
                                     " stored records during erase: the representation did not do what "
                                     "it claims, whatever the content comparison says");

        std::cout << "{\"phase\":\"batch\",\"name\":\"overflow_erase\""
                  << ",\"steps\":" << erase_steps.size()
                  << ",\"arm_a_overflow_released\":" << a_overflow_released
                  << ",\"arm_a_logical_shifts\":" << a_logical_shifts
                  << ",\"arm_b_logical_shifts\":" << b_logical_shifts
                  << ",\"arm_b_handle_moves\":" << b_handle_moves
                  << ",\"arm_b_record_moves\":" << b_record_moves
                  << "}" << std::endl;

        checkpoint("overflow_erase");

        // ---- Batch 3: re-insert into the same groups ----------------------------
        // The erased records left holes in the handle order, so the re-inserts aim at the
        // SAME groups through the same bases: an insert that has to land in the middle of the
        // order is exercised, not just an append.
        std::vector<Step> reinsert;
        {
            std::vector<std::string> bases;
            for (const auto& s : erase_steps) {
                const auto position = s.key.find('\x02');
                if (position == std::string::npos)
                    throw std::runtime_error("a victim key is not a forced-overflow key: " + s.key);
                bases.push_back(s.key.substr(0, position));
            }
            std::sort(bases.begin(), bases.end());
            bases.erase(std::unique(bases.begin(), bases.end()), bases.end());

            std::size_t serial = 0;
            for (const auto& base : bases) {
                for (std::size_t k = 0; k < 8; ++k) {
                    std::string fresh = base;
                    fresh.push_back('\x03');
                    fresh += std::to_string(serial);
                    ++serial;
                    if (!used.insert(fresh).second)
                        throw std::runtime_error("the re-insert key was not fresh: " + fresh);
                    reinsert.push_back(Step{1, fresh, 0xB0000000ULL + serial});
                }
            }
        }

        fold_reference(ref, reinsert, 0, reinsert.size());
        apply_steps(*a, reinsert, 0, reinsert.size(), "arm A");
        apply_steps(*b, reinsert, 0, reinsert.size(), "arm B");

        std::cout << "{\"phase\":\"batch\",\"name\":\"reinsert\""
                  << ",\"steps\":" << reinsert.size()
                  << ",\"arm_a_overflow\":" << a->overflow_count()
                  << ",\"arm_b_overflow\":" << b->overflow_count()
                  << "}" << std::endl;

        checkpoint("reinsert");

        // ---- Point queries -------------------------------------------------------
        // Every key the reference holds, plus a sample of keys it does not. The absent probes
        // are a reference key with a byte appended that no generated key carries.
        for (const auto& kv : ref)
            assert_find_agrees(*a, *b, kv.first, qmode, true, kv.second, finds);

        {
            const std::size_t stride = std::max<std::size_t>(1, ref.size() / 4096);
            std::size_t index = 0;
            std::size_t absent = 0;

            for (const auto& kv : ref) {
                if (index % stride == 0) {
                    std::string missing = kv.first;
                    missing.push_back('\x01');
                    if (ref.count(missing))
                        throw std::runtime_error("the absent-probe key is present in the reference: " + missing);
                    assert_find_agrees(*a, *b, missing, qmode, false, 0, finds);
                    ++absent;
                }
                ++index;
            }

            std::cout << "{\"phase\":\"find\",\"kind\":\"" << kind << "\""
                      << ",\"present_probes\":" << ref.size()
                      << ",\"absent_probes\":" << absent
                      << ",\"compared\":" << finds << "}" << std::endl;
        }

        // ---- Scans ---------------------------------------------------------------
        {
            const std::size_t counts[3] = {1, 2, 16};
            std::vector<std::string> starts;
            starts.push_back(std::string());

            const std::size_t stride = std::max<std::size_t>(1, ref.size() / 256);
            std::size_t index = 0;
            for (const auto& kv : ref) {
                if (index % stride == 0) starts.push_back(kv.first);
                ++index;
            }

            // Beyond the last key: the scan must come back empty on both arms.
            starts.push_back(ref.rbegin()->first + '\x7f');

            for (const auto& start : starts)
                for (std::size_t c = 0; c < 3; ++c)
                    assert_scan_agrees(*a, *b, ref, start, counts[c], scans, scan_records);

            assert_scan_agrees(*a, *b, ref, std::string(), ref.size(), scans, scan_records);
        }

        // ---- The representation, measured at the END of the run ------------------
        // The counters below are written only inside the stable container, so these are arm
        // B's numbers and nothing else: no other arm in this process can move them.
        const std::uint64_t handle_moves_total = e25::counts().overflow_handle_moves;
        const std::uint64_t record_moves_total = e25::counts().overflow_record_moves_stable;
        const std::uint64_t store_high_water = e25::counts().ov_store_high_water;
        const std::uint64_t store_live_water = e25::counts().ov_store_live_water;

        if (handle_moves_total == 0)
            throw std::runtime_error("the stable arm never shifted a handle over the whole run: the "
                                     "switch never took effect, and this gate proved nothing");

        if (record_moves_total != 0)
            throw std::runtime_error("the stable store relocated " + std::to_string(record_moves_total) +
                                     " records over the whole run: the point of O1 is that it relocates "
                                     "none, so the representation is not doing what it claims");

        if (store_live_water == 0)
            throw std::runtime_error("the stable store never held a record: the arm that was supposed "
                                     "to exercise it did not");

        if (store_high_water < store_live_water)
            throw std::runtime_error("the store's high-water mark is below its live peak: the hole "
                                     "reuse the representation promises did not happen");

        std::cout << "{\"gate\":\"check_overflow_store\",\"kind\":\"" << kind << "\""
                  << ",\"run_tag\":\"" << run_tag << "\""
                  << ",\"keys\":" << N
                  << ",\"regions\":" << a->region_count()
                  << ",\"result\":\"pass\""
                  << ",\"records_compared\":" << compared
                  << ",\"finds_compared\":" << finds
                  << ",\"scans\":" << scans
                  << ",\"scan_records\":" << scan_records
                  << ",\"arm_a_overflow\":" << a->overflow_count()
                  << ",\"arm_b_overflow\":" << b->overflow_count()
                  << ",\"arm_a_memory\":" << a->memory()
                  << ",\"arm_b_memory\":" << b->memory()
                  << ",\"handle_moves_total\":" << handle_moves_total
                  << ",\"record_moves_total\":" << record_moves_total
                  << ",\"store_high_water\":" << store_high_water
                  << ",\"store_live_water\":" << store_live_water
                  << "}" << std::endl;

        std::cerr << kind << " overflow-store gate done" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED " << e.what() << std::endl;
        return 1;
    }
}

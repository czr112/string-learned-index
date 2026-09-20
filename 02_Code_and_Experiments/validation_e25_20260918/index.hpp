#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

// ---------------------------------------------------------------------------
// E14: build-phase instrumentation.
//
// Everything below is compiled out unless -DE14_PHASE is defined. index.hpp is
// therefore unchanged in behaviour with and without the flag -- the only
// difference is whether these accumulators are incremented. The gates are built
// WITHOUT the flag, so their "unchanged" evidence still holds, and the layout a
// -DE14_PHASE build produces is asserted byte-equal to a build without it by
// bench_e14 before any number is reported.
//
// Plain integers, not atomics: every build measured here is single-threaded and
// the process is bound to one core.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// E18: activity counters for the construction path.
//
// NO clock calls here, on purpose. The operations being counted are very short
// (a model prediction, a byte copy, a descriptor shift), so a timer around each
// one would cost more than the thing it measures and would change the cost
// structure under study. Counts are cheap and are the input to deciding which
// stage to time coarsely next -- see the round's RESULTS.
//
// Compiled out unless -DE18_COUNT.
// ---------------------------------------------------------------------------
#ifdef E18_COUNT
namespace validation {
namespace e18 {
struct Counts {
    std::uint64_t group_calls = 0;          // model predictions for a group id
    std::uint64_t fingerprint_calls = 0;    // Fingerprint::of
    std::uint64_t region_ctors = 0;
    std::uint64_t subblock_probes = 0;      // put_primary loop iterations
    std::uint64_t insert_attempts = 0;      // insert_into calls
    std::uint64_t insert_ok = 0;            // ... of which found room
    std::uint64_t entry_shifts = 0;         // descriptors moved to make room
    std::uint64_t suffix_bytes_copied = 0;  // bytes memcpy'd into a block
    std::uint64_t overflow_inserts = 0;     // records sent to an overflow list
    std::uint64_t overflow_containers = 0;  // overflow lists created
    void reset() { *this = Counts{}; }
};
inline Counts& counts() { static Counts c; return c; }
}  // namespace e18
}  // namespace validation
#define E18_INC(field) (++::validation::e18::counts().field)
#define E18_ADD(field, n) (::validation::e18::counts().field += (n))
#else
#define E18_INC(field) ((void)0)
#define E18_ADD(field, n) ((void)0)
#endif

// ---------------------------------------------------------------------------
// E24: update-path and space diagnostics.
//
// The E23 audit found Region::update and Region::erase linear-scan every
// sub-block with a FULL-KEY compare, while find/insert use the fingerprint tag
// filter, the length pre-check, the boundary directory and the overflow Hybrid.
// These counters record what the two paths actually walk, so the asymmetry is
// MEASURED rather than read off the source.
//
// e24_group_report() exposes the live per-group geometry that the space
// decomposition and the reclaim analysis need. It walks the real structure --
// it is not the static GroupStats model, which describes an INPUT, not the
// current layout.
//
// Compiled out unless -DE24_DIAG. Nothing here is on the timing path.
// ---------------------------------------------------------------------------
#ifdef E30_TIME
// E30: how much of a replacement workload is spent INSIDE compact()?
// Direct measurement, so it does not have to be inferred from the difference between
// two configurations -- that inference is what produced two answers 5.5x apart.
#include <chrono>
namespace validation {
namespace e30 {
struct Timers {
    std::uint64_t compact_ns = 0, compact_calls = 0;
    std::uint64_t collect_ns = 0;   // gathering live records out of the sub-blocks
    std::uint64_t replay_ns = 0;    // put_primary replay + directory rebuild
    std::uint64_t erase_ns = 0, erase_calls = 0;
    std::uint64_t insert_ns = 0, insert_calls = 0;
    // E31 item 3 -- the checklist's section 5 PRECONDITION. The ORDERED OVERFLOW
    // VECTOR's own time: put_overflow's lower_bound + element shift + std::string
    // construction, and erase_at's vector erase + destruction. Without it the share the
    // O1/O2/O3 choice depends on is an assumption, and the audit found it has never been
    // isolated (E25-E31 carry no timing for the container itself).
    //
    // These NEST inside erase_ns (and inside insert_slow's insert_ns for the placements
    // that come from it), so a ratio against either is a SHARE, never an additive term.
    // Note that insert_ns is 0 in the frozen arm -- V2 routes a new key through
    // place_with_ctx, which starts no clock -- which is exactly why the diagnostic times
    // the segment itself instead of dividing by insert_ns.
    std::uint64_t ov_insert_ns = 0, ov_insert_calls = 0;
    std::uint64_t ov_erase_ns = 0, ov_erase_calls = 0;
    void reset() { *this = Timers{}; }
};
inline Timers& timers() { static Timers t; return t; }
inline std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace e30
}  // namespace validation
#define E30_T0() ::validation::e30::now_ns()
#define E30_ADD(f, t0) (::validation::e30::timers().f += ::validation::e30::now_ns() - (t0))
#define E30_INC(f) (++::validation::e30::timers().f)
#else
#define E30_T0() 0
#define E30_ADD(f, t0) ((void)0)
#define E30_INC(f) ((void)0)
#endif

#ifdef E24_DIAG
namespace validation {
namespace e24 {
struct Counts {
    // Region::update -- the same key set find() is measured on.
    std::uint64_t upd_cmps = 0, upd_bytes = 0, upd_entries = 0, upd_subblocks = 0;
    // Region::erase -- on a separate, discarded index.
    std::uint64_t era_cmps = 0, era_bytes = 0, era_entries = 0, era_subblocks = 0;
    void reset() { *this = Counts{}; }
};
inline Counts& counts() { static Counts c; return c; }

// One live sub-block of a group, as it exists NOW.
struct SubBlock {
    std::size_t slots = 0;        // capacity in slots
    std::size_t used = 0;         // live entries
    std::size_t suffix_cap = 0;   // bb - start_of, the whole suffix area
    std::size_t suffix_live = 0;  // sum of live entry lengths
    std::size_t suffix_free = 0;  // bottom - start_of: bytes that can still take a record
    std::size_t occupied = 0;     // occupied region = suffix_cap - suffix_free
    std::size_t garbage = 0;      // occupied - suffix_live: deleted bytes not yet reclaimed
};
struct GroupReport {
    std::uint32_t group = 0;
    std::vector<SubBlock> subs;   // empty when the group has no primary sub-block
    std::size_t ov_records = 0;
    std::size_t ov_live_bytes = 0;    // sum of live overflow suffix lengths
    std::size_t ov_suffix_cap = 0;    // sum of suffix.capacity()+1 over live records
    std::size_t ov_container_cap = 0; // container capacity * sizeof(OverflowRecord)
    std::size_t ov_rec_size = 0;      // sizeof(OverflowRecord), so slack is exact
    // Reclaim: an overflow record may move back only into the ONE sub-block its key
    // order selects (M1), and only if THAT sub-block has a free slot AND room for its
    // length. "The group has free space" is not the same test.
    std::size_t ov_fits_ordered = 0;      // passes slot + bytes + key-order
    std::size_t ov_fits_ordered_bytes = 0;
    std::size_t ov_blocked_slots = 0;     // ordered sub-block exists, no free slot
    std::size_t ov_blocked_bytes = 0;     // slot free, not enough suffix bytes
    std::size_t ov_blocked_order = 0;     // sorts below every primary sub-block
    std::size_t ov_fits_if_any_subblock = 0;  // what the existing group_has_room() would say
};
}  // namespace e24
}  // namespace validation
// The compare macros expand at their use site (deep inside Region), where both
// compare_bytes and the counter struct are visible -- so they carry the work
// themselves rather than calling a helper that would have to be declared here.
#define E24_CMP_UPD(a, b) \
    (++::validation::e24::counts().upd_cmps, \
     compare_bytes((a), (b), ::validation::e24::counts().upd_bytes))
#define E24_CMP_ERA(a, b) \
    (++::validation::e24::counts().era_cmps, \
     compare_bytes((a), (b), ::validation::e24::counts().era_bytes))
#define E24_UP_ENTRY() (++::validation::e24::counts().upd_entries)
#define E24_ERA_ENTRY() (++::validation::e24::counts().era_entries)
#define E24_UP_BLOCK() (++::validation::e24::counts().upd_subblocks)
#define E24_ERA_BLOCK() (++::validation::e24::counts().era_subblocks)
#else
#define E24_CMP_UPD(a, b) compare((a), (b))
#define E24_CMP_ERA(a, b) compare((a), (b))
#define E24_UP_ENTRY() ((void)0)
#define E24_ERA_ENTRY() ((void)0)
#define E24_UP_BLOCK() ((void)0)
#define E24_ERA_BLOCK() ((void)0)
#endif

// ---------------------------------------------------------------------------
// E25: the call matrix. What each write path actually invokes, per operation
// category, so "repeated location" is a counted fact rather than an inference
// from a large comparison ratio.
//
// FINGERPRINT ACCOUNTING. Counting Fingerprint::of() calls alone would be
// misleading: the model prediction hashes the bytes it consumes (fp.byte) and
// find_impl finishes the key with fp.tail, so the FUSED path produces a full
// fingerprint without ever calling Fingerprint::of. A change in code shape could
// therefore make "fingerprint computation" look like it disappeared when it only
// moved. So bytes actually fed to the hash are counted at the byte()/tail() level,
// which is independent of which call site produced them.
//
// Branch counters exist so the gate can assert that a branch RAN, instead of
// inferring coverage from "the final content matched" -- which is exactly the
// inference that missed the Index-level control flow E25 broke once.
//
// Compiled out unless -DE25_DIAG. Never on a timing path.
// ---------------------------------------------------------------------------
#ifdef E25_DIAG
namespace validation {
namespace e25 {
struct Counts {
    std::uint64_t locate_calls = 0;        // Index::locate (outer routing)
    std::uint64_t predict_calls = 0;       // Model::predict
    std::uint64_t fp_of_calls = 0;         // Fingerprint::of, whole-key form
    std::uint64_t fp_hash_bytes = 0;       // bytes fed to Fingerprint::byte/tail
    std::uint64_t existence_searches = 0;  // Region::find_impl entries
    std::uint64_t cmp_calls = 0;           // full-key compares in the write paths
    std::uint64_t cmp_bytes = 0;
    std::uint64_t place_attempts = 0;      // insert_into calls
    std::uint64_t place_fail_slots = 0;    // ... refused: no free slot
    std::uint64_t place_fail_bytes = 0;    // ... refused: not enough suffix bytes
    std::uint64_t compactions = 0;         // Region::compact runs
    std::uint64_t region_rebuilds = 0;     // Index::rebuild calls
    // Erase work. `erase_shifts` counts descriptors moved by the shift-down, and
    // `erase_released` counts suffix bytes whose record went away -- for a primary
    // erase those bytes stay in the block as garbage, for an overflow erase the
    // string is actually destroyed. The two are reported apart because only the
    // second is a real free().
    std::uint64_t erase_shifts = 0;
    std::uint64_t erase_released_primary = 0;
    std::uint64_t erase_released_overflow = 0;
    std::uint64_t string_allocs = 0;       // std::string constructed into a container
    std::uint64_t string_frees = 0;        // record destroyed, its string released

    // ---- E26: what compaction actually does ---------------------------------
    // compact() is entered from ONE place: a failed primary placement in a group
    // that still reports room somewhere. Whether the retry then SUCCEEDS is the
    // direct measure of whether that compaction did anything for the operation that
    // triggered it. A failure to help is recorded as "did not enable this placement",
    // which is NOT the same as "the compaction had no value" -- it also redistributes
    // records across sub-blocks, and that can matter for later operations.
    std::uint64_t compact_entered = 0;      // times the branch was taken
    std::uint64_t compact_helped = 0;       // the retry placed the record in the primary
    std::uint64_t compact_no_help = 0;      // the retry still failed -> overflow
    std::uint64_t compact_records_replayed = 0;
    std::uint64_t compact_bytes_copied = 0;
    std::uint64_t compact_garbage_before = 0;   // un-referenced bytes in the group, pre-compact
    std::uint64_t compact_tag_mismatch = 0;     // E26 diag: stored tag != recomputed tag
    // The three branches E25's rewrite touched. Asserted by the gate.
    std::uint64_t notlocated_fallback = 0; // V2 upsert: no context, took the old path
    std::uint64_t subblock_reclaims = 0;        // E31: in-place sub-block passes run
    std::uint64_t subblock_reclaim_helped = 0;  // ... of which made the retry fit
    std::uint64_t reclaim_skipped_unchanged = 0;  // E31H: skipped, garbage not grown
    std::uint64_t reclaim_skipped_gap = 0;        // E31H: skipped, min operation gap unmet
    std::uint64_t prefix_rebuilds = 0;     // Index::insert prefix-mismatch rebuild
    std::uint64_t threshold_splits = 0;    // Index::insert region split at 2*region_keys

    // ---- E27: what a range scan actually does -------------------------------
    // The four quantities the review named, kept apart so the round can pick the
    // ONE that matters instead of optimizing all four at once.
    std::uint64_t scan_regions = 0;          // regions entered by Index::scan
    std::uint64_t scan_groups = 0;           // groups iterated inside a region
    std::uint64_t scan_groups_empty = 0;     // ... of which held nothing at all
    std::uint64_t scan_blocks_skipped = 0;   // empty/exhausted sub-blocks stepped over
    std::uint64_t scan_merge_cmps = 0;       // primary-vs-overflow ordering compares
    std::uint64_t scan_result_allocs = 0;    // std::string constructions for results
    // ---- E31 item 3: the overflow vector's MOVEMENT, as bytes ------------------
    // "The ordered overflow vector's movement is a large share of the time" is the claim
    // the checklist wants before O1/O2/O3 is chosen, so the movement is recorded as a
    // number rather than an adjective. The element count is read at the one point where
    // the distance is still valid (before the container is touched), and the byte figure
    // is that count times the record's OWN size: a shift copies each OverflowRecord's 48
    // bytes and NOT the suffix buffer it points at, so a figure that counted the payload
    // would overstate what the container actually moves. The element size is reported as
    // ov_rec_bytes, so the byte counts can be read back as record counts.
    std::uint64_t ov_shifted_records_insert = 0;
    std::uint64_t ov_shifted_bytes_insert = 0;
    std::uint64_t ov_shifted_records_erase = 0;
    std::uint64_t ov_shifted_bytes_erase = 0;
    std::uint64_t scan_start_locates = 0;    // E27 S2: locate(start) evaluations in scan()
    void reset() { *this = Counts{}; }
};
inline Counts& counts() { static Counts c; return c; }
}  // namespace e25
}  // namespace validation
#define E25_INC(f) (++::validation::e25::counts().f)
#define E25_ADD(f, n) (::validation::e25::counts().f += (n))
// One scale for BOTH search paths: the fast locate and the slow linear walk must
// report their string comparisons into the same counters, or the three variants
// would be measured with two different rulers. Expands at the use site, where
// compare_bytes is visible.
#define E25_CMP(a, b) \
    (++::validation::e25::counts().cmp_calls, \
     compare_bytes((a), (b), ::validation::e25::counts().cmp_bytes))
#else
#define E25_INC(f) ((void)0)
#define E25_ADD(f, n) ((void)0)
#define E25_CMP(a, b) compare((a), (b))
#endif

#ifdef E14_PHASE
#include <chrono>

namespace validation {
namespace e14 {

struct Phases {
    // Region constructor
    std::uint64_t prefix = 0;        // common-prefix computation
    std::uint64_t model = 0;         // model_.train
    std::uint64_t suffix_scan = 0;   // suffix-byte total over the region
    std::uint64_t demand_pass = 0;   // E9 pass 1: per-group entries/bytes demand
    std::uint64_t layout = 0;        // group count, capacity table, off_ derivation
    std::uint64_t storage = 0;       // operator new + header/entry initialisation
    std::uint64_t place = 0;         // initial placement (put_primary/put_overflow)
    std::uint64_t dir = 0;           // boundary-directory generation
    std::uint64_t grp_prep = 0;      // E18: hoisted group-id + fingerprint pre-pass
    std::uint64_t ctor_total = 0;    // whole constructor, for reconciliation
    std::uint64_t ctor_calls = 0;

    // Index level
    std::uint64_t bulk_load = 0;     // Index::bulk_load, incl. the Region ctors
    std::uint64_t bulk_planned = 0;  // E15: Index::bulk_load_with_plan, ditto
    std::uint64_t rebuild = 0;       // rebuild_with_caps / rebuild, incl. Region ctor
    std::uint64_t rebuild_calls = 0;
    std::uint64_t region_all = 0;    // Region::all(), the sorted copy handed to a rebuild

    void reset() { *this = Phases{}; }
};

inline Phases& phases() { static Phases p; return p; }

inline std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace e14
}  // namespace validation

#define E14_T0() ::validation::e14::now_ns()
#define E14_ADD(field, t0) (::validation::e14::phases().field += ::validation::e14::now_ns() - (t0))
#define E14_INC(field) (++::validation::e14::phases().field)
#else
#define E14_T0() 0
#define E14_ADD(field, t0) ((void)0)
#define E14_INC(field) ((void)0)
#endif

namespace validation {

// ---------------------------------------------------------------------------
// Ordering contract: unsigned-byte lexicographic comparison, shared by every
// structure (model grouping, in-block ordering, outer routing, reference map).
// ---------------------------------------------------------------------------

inline int compare(std::string_view a, std::string_view b) {
    const auto n = std::min(a.size(), b.size());
    const int c = n ? std::memcmp(a.data(), b.data(), n) : 0;
    return c ? c : (a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0);
}

struct Less {
    bool operator()(std::string_view a, std::string_view b) const {
        return compare(a, b) < 0;
    }
};

using Pair = std::pair<std::string, std::uint64_t>;

inline bool pair_less(const Pair& a, const Pair& b) {
    return compare(a.first, b.first) < 0;
}

// ---------------------------------------------------------------------------
// Fingerprint: CRC-32C tag used only as a candidate filter; a matching tag never
// proves equality, every hit goes through the exact compare.
// ---------------------------------------------------------------------------

struct Fingerprint {
    std::uint32_t state = 0x9e3779b9U;

    void byte(std::uint8_t x) {
        E25_ADD(fp_hash_bytes, 1);
#if defined(__SSE4_2__)
        state = _mm_crc32_u8(state, x);
#else
        state ^= x;
        for (int i = 0; i < 8; ++i)
            state = (state >> 1) ^ (0x82f63b78U & (0U - (state & 1U)));
#endif
    }

    void tail(std::string_view x) {
#if defined(__SSE4_2__)
        // Count ONLY the bytes the 8-byte chunk loop consumes: the remainder goes
        // through byte(), which counts itself. Counting x.size() here as well would
        // double-count the tail.
        const std::size_t before = x.size();
        while (x.size() >= 8) {
            std::uint64_t w;
            std::memcpy(&w, x.data(), 8);
            state = static_cast<std::uint32_t>(_mm_crc32_u64(state, w));
            x.remove_prefix(8);
        }
        E25_ADD(fp_hash_bytes, before - x.size());
#endif
        for (unsigned char c : x) byte(c);
    }

    std::uint8_t tag() const { return static_cast<std::uint8_t>(state); }

    static std::uint8_t of(std::string_view s) {
        E25_INC(fp_of_calls);
        Fingerprint f;
        f.tail(s);
        return f.tag();
    }
};

// ---------------------------------------------------------------------------
// Lookup configuration and per-query result.
// ---------------------------------------------------------------------------

enum class Route { Full, Interval, Residual };
enum class Prefetch { Off, Early, Late };

// E5-2: mode.simd selects the batched SSE2 tag filter; identical results by
// construction.
// E7: overflow point-lookup policy rides on the per-query Mode (not Config):
// the overflow records and their sorted order are identical for every policy,
// so all variants measure the same records with a different lookup algorithm.
// E12: diagnostic counters are compiled OUT of the timing build. They never
// influence found/value, but a timed lookup should not pay for them either, so the
// benchmark is built without E12_COUNT while the gates are built with it.
#ifdef E12_COUNT
#define E12_TICK(x) (++(x))
#define E12_ADD(x, n) ((x) += (n))
#else
#define E12_TICK(x) ((void)0)
#define E12_ADD(x, n) ((void)0)
#endif

enum class OvSearch { Linear, Binary, Hybrid };

struct Mode {
    Route route;
    Prefetch prefetch;
    bool fingerprint;
    bool simd = false;
    OvSearch ov = OvSearch::Linear;
    std::uint32_t ov_thr = 32;
    // E11: for multi-block groups, use the boundary directory to pick at most one
    // candidate primary block instead of scanning them in order. Off by default,
    // so the path is bit-identical to E10 unless a caller asks for it.
    bool locate = false;
};

struct Result {
    bool found = false;
    std::uint64_t value = 0;
    std::uint32_t steps = 0;
    std::uint32_t overflow_checks = 0;
    std::uint32_t str_cmps = 0;   // E7: full string comparisons for this lookup
    std::uint8_t source = 0;      // E7: 0 miss, 1 primary hit, 2 overflow hit
    std::uint64_t cmp_bytes = 0;  // E7 diag: bytes walked by compares (E7_COUNT_BYTES builds)

    // E10 access counters: what the lookup path actually executes. Only meaningful
    // for a query that got past the prefix check -- one rejected there returns all
    // zeros. They never influence found/value, and E10 takes no timings, so leaving
    // them always on costs nothing that is measured.
    std::uint32_t subblocks_visited = 0;   // sub-blocks entered (empty ones count: header read)
    std::uint32_t simd_batches = 0;        // vector tag compares actually executed
    std::uint32_t tag_candidates = 0;      // tag matches surviving the live-slot mask
    std::uint32_t str_cmps_primary = 0;    // exact compares against in-block entries
    std::uint32_t str_cmps_overflow = 0;   // exact compares against overflow records
    std::uint32_t overflow_entered = 0;    // 1 if the overflow container was searched

    // E11: location cost kept separate from the block-check cost, because the goal
    // is "at most one candidate block checked", not "total access down to one".
    std::uint32_t locate_probes = 0;       // boundary comparisons during location
    std::uint64_t locate_cmp_bytes = 0;    // logical bytes walked by those comparisons
    std::uint32_t candidate_blocks = 0;    // primary blocks entered for tag/record checks
};

// E5-1: slots is an explicit layout parameter (default 16 reproduces the E3 behavior).
enum class ModelKind { Context16, Position16, Unigram };

struct Config {
    std::uint32_t block_bytes = 1024;
    double load = .75;
    std::size_t region_keys = 2048;
    Route canonical = Route::Full;
    std::uint32_t fixed_groups = 0;
    ModelKind model_kind = ModelKind::Context16;
    std::uint32_t slots = 16;
    // E8-11: false = legacy fixed-stride addressing (base + g*block_bytes, uniform
    // capacity only); true = offset-directory addressing (required for demand caps).
    bool offset_directory = true;
    // E9: when true, a group whose demand exceeds one sub-block grows to several
    // equal-sized sub-blocks within the same table shape. Off by default, so the
    // single-block path stays bit-identical to E5-E8 and their gates keep passing.
    bool multiblock = false;
    // E19 (R1): reuse the group ids the demand pass computed, instead of calling
    // canonical_group a second time for the same key in the placement loop.
    // Only meaningful with multiblock = true, which is where the demand pass runs.
    // The fingerprint is NOT affected -- it is computed once, in placement.
    bool reuse_group_ids = false;

    // E25 V1: update/erase search through the SAME machinery find() uses -- the
    // fingerprint tag filter, the SIMD batch filter, the boundary directory and the
    // overflow policy -- instead of walking every entry with a full-key compare.
    // Off reproduces the E24 behaviour exactly, so V0/V1 are one binary apart.
    // ---- FROZEN 2026-09-20: these five defaults are ON ----
    // Chosen on the same-round evidence in E25 (write path), E26 (compaction) and
    // E27 (scan), not on call counts. The build path (R1), the model, the logical
    // mapping and the main-block budget are unchanged.
    // Every gate pins its arms through `switches.hpp`, so these defaults are the
    // SHIPPED configuration and not silently the experimental baseline.
    bool write_fast_locate = true;

    // E25 V2: insert/upsert reuse the group id, the tag and the "not present"
    // conclusion the SAME operation's locate already produced, instead of asking
    // the model and the fingerprint again. Duplicate detection is NOT skipped.
    bool reuse_locate_ctx = true;

    // E25: the mode the WRITE paths search with (V1). Defaults mirror the query
    // mode the benchmarks use: fingerprint + SIMD + Hybrid overflow, and NOT the
    // boundary locate -- E13 established locate is a selectable component, not a
    // default. ov_threshold is the frozen Hybrid cutoff, set by the caller.
    std::uint32_t ov_threshold = 32;
    bool write_simd = true;
    bool write_locate = false;

    // E26 C1: compact() currently re-derives Fingerprint::of() for every record it
    // replays. The tag of a record does not depend on WHERE the record sits, and the
    // suffix bytes are copied verbatim, so the stored tag is still correct and can
    // travel with the record. Changing this must not alter membership, placement
    // order, capacity, the trigger, or the boundary maintenance -- it only removes a
    // recomputation.
    bool compact_reuse_tag = true;

    // E26 C2: compact() reclaims BYTES by re-packing suffixes; it creates no slots.
    // So when a group holds less reclaimable garbage than the record that just failed
    // to place, that compaction cannot free enough for it. This gate skips those. It
    // is a NECESSARY-condition test, not a model of when compaction pays: compaction
    // also redistributes records across sub-blocks, which this does not predict.
    bool compact_needs_garbage = true;

    // E27: the scan result is `prefix_ + std::string(suffix)`, which builds a
    // temporary string and then concatenates it -- two allocations per returned
    // record. Building the result string in place with one reservation does the same
    // job with one. Semantics (complete key/value pairs, key order, ownership) are
    // unchanged; only the intermediate copy goes away.
    bool scan_reserve_result = true;

    // E29 C3: relax the compaction TRIGGER. Compaction is currently attempted only
    // when some single sub-block has both a free slot and enough suffix bytes -- and
    // the E28 measurement showed that is exactly the condition that is usually false
    // while the group still holds plenty of garbage. C3 additionally compacts when a
    // full repack of the group would fit the record.
    //
    // The budget test is not optional: compact() replays every live record and throws
    // if the group cannot hold them, so it must never be invoked on a short budget.
    bool compact_repack_budget = false;

    // E31: reclaim the rank-selected sub-block IN PLACE before giving up on the
    // primary. erase() frees a slot but leaves the suffix bytes as garbage and does not
    // move the sub-block's bottom watermark, so free bytes do not increase -- which is
    // why a placement that needs bytes fails while the sub-block still holds plenty of
    // reclaimable space. E30 measured this pass at 6.1-11.4 ns/record against 163 ns for
    // the frozen arm's whole-group compaction, and the whole-run reclaim against the
    // compaction it displaces by 8.1x-21.3x.
    //
    // The sub-block BOUNDARY does not change, so order invariant M1 is untouched and
    // the E25 Located context stays valid.
    bool subblock_reclaim = false;

    // E31H (checklist item 4.3): hysteresis and a minimum operation interval on the
    // reclaim trigger. E31's own measurement (2026-09-20) fired it 127,780 times (mixed
    // same) to 255,882 (skew grow) at the 200% checkpoint and enabled the triggering
    // placement in only 20.4%-67.2% of them, so most of the rest re-ran the pass on a
    // sub-block the previous attempt had already left unchanged. With this ON a group's
    // reclaim is skipped unless at least kMinReclaimGap placements have happened since its
    // last attempt AND its garbage has grown since then. Default OFF: the frozen
    // combination and every existing gate keep the E31 behaviour byte for byte.
    bool reclaim_hysteresis = false;

    // E31P (checklist 4.3, the other half of the same rule): skip a repeated reclaim
    // only when the group's garbage has not grown since its last attempt -- the
    // checklist's "do not retry while the state has not materially changed" -- with no
    // minimum interval. The interval half is reclaim_hysteresis; measuring them apart
    // is the point, because the interval is what turned out to bind.
    bool reclaim_skip_repeat = false;

    // E27 S2 (checklist 7.1 / 9 row 4): Index::scan re-evaluated locate(start)
    // on every loop iteration AND once more in the body, so a scan spanning R
    // regions paid R+1 outer searches for the one answer it already had. Caching
    // the first evaluation makes it exactly one. The evaluations are counted
    // (scan_start_locates), so the saving is measured rather than argued: the S0
    // arm counts R+1 per scan, the S2 arm exactly 1.
    bool scan_cache_start = false;
};

#if defined(E7_COUNT_BYTES) || defined(E24_DIAG) || defined(E25_DIAG)
// Diagnostic builds only: mirrors compare() while counting bytes actually walked.
// memcmp stops at the first difference, so one call does not imply a full scan.
inline int compare_bytes(std::string_view a, std::string_view b, std::uint64_t& bytes) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && static_cast<unsigned char>(a[i]) == static_cast<unsigned char>(b[i])) ++i;
    bytes += (i < n) ? i + 1 : n;
    int c = 0;
    if (i < n)
        c = static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[i]) ? -1 : 1;
    else
        c = a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
    return c;
}
#endif

// ---------------------------------------------------------------------------
// Conditional model: integer interval arithmetic over quantized probabilities.
// Q = 2^47 position scale, MASS = 2^16 probability mass, 16 context rows.
// ---------------------------------------------------------------------------

class Model {
public:
    static constexpr std::uint64_t Q = 1ULL << 47;
    static constexpr std::uint32_t MASS = 1U << 16;
    static constexpr std::size_t ROWS = 16;

    using Table = std::vector<std::array<std::uint32_t, 258>>;

    Table cumulative;
    ModelKind kind = ModelKind::Context16;

    std::size_t row(std::size_t pos, unsigned previous) const {
        if (kind == ModelKind::Unigram) return 0;
        if (kind == ModelKind::Position16) return std::min<std::size_t>(15, pos);
        return ((pos & 3U) << 2U) | (previous & 3U);
    }

    std::size_t allocated_bytes() const {
        return cumulative.capacity() * sizeof(Table::value_type);
    }

    // E16: a range overload. The body is the original loop with the container
    // iteration replaced by pointer iteration; the vector overload forwards, so
    // every existing caller trains exactly as before.
    void train(const std::vector<Pair>& keys, std::size_t prefix,
               ModelKind choice = ModelKind::Context16) {
        train(keys.data(), keys.data() + keys.size(), prefix, choice);
    }

    void train(const Pair* first, const Pair* last, std::size_t prefix,
               ModelKind choice = ModelKind::Context16) {
        kind = choice;
        const auto rows = kind == ModelKind::Unigram ? 1 : ROWS;
        cumulative.assign(rows, {});

        std::vector<std::array<std::uint64_t, 257>> counts(rows);
        for (auto& r : counts) r.fill(1);  // finite smoothing, including EOS and byte 0

        for (const Pair* it = first; it != last; ++it) {
            const auto s = std::string_view(it->first).substr(prefix);
            unsigned previous = 0;
            const auto lim = std::min<std::size_t>(16, s.size());
            for (std::size_t p = 0; p < lim; ++p) {
                unsigned d = static_cast<unsigned char>(s[p]) + 1U;
                ++counts[row(p, previous)][d];
                previous = d;
            }
            if (s.size() <= 16) ++counts[row(s.size(), previous)][0];
        }

        for (std::size_t r = 0; r < rows; ++r) {
            std::uint64_t total = 0;
            for (auto x : counts[r]) total += x;

            std::array<std::uint32_t, 257> frequency{};
            std::uint32_t sum = 0;
            for (std::size_t d = 0; d < 257; ++d) {
                frequency[d] = static_cast<std::uint32_t>(counts[r][d] * MASS / total);
                sum += frequency[d];
            }
            // Deterministic remainder assignment; at most 256 residual units.
            for (std::uint32_t d = 0; d < MASS - sum; ++d) ++frequency[d];

            cumulative[r][0] = 0;
            for (std::size_t d = 0; d < 257; ++d)
                cumulative[r][d + 1] = cumulative[r][d] + frequency[d];
            assert(cumulative[r][257] == MASS);
        }
    }

    // Interval step; both endpoints derive from the pre-update lo to avoid
    // ordering mistakes. W <= 2^47 and C <= 2^16, so the product fits uint64.
    static void step(const std::array<std::uint32_t, 258>& c, unsigned d,
                     std::uint64_t& lo, std::uint64_t& hi) {
        const std::uint64_t origin = lo, w = hi - lo;
        lo = origin + ((w * c[d]) >> 16);
        hi = origin + ((w * c[d + 1]) >> 16);
    }

    // Physical mapping h: monotone in x, hence group order follows key order.
    static std::uint32_t bucket(std::uint64_t x, std::uint32_t groups) {
        const auto g = static_cast<std::uint64_t>((static_cast<unsigned __int128>(x) * groups) >> 47);
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(groups - 1, g));
    }

    struct Prediction {
        std::uint32_t group;
        std::uint32_t consumed;
    };

    template <bool Hash>
    Prediction predict(std::string_view s, std::uint32_t groups, Route route,
                       Fingerprint& fp) const {
        E25_INC(predict_calls);
        std::uint64_t lo = 0, hi = Q;
        unsigned previous = 0;
        std::size_t p = 0;
        for (; p < s.size(); ++p) {
            const auto ch = static_cast<unsigned char>(s[p]);
            step(cumulative[row(p, previous)], ch + 1U, lo, hi);
            previous = ch + 1U;
            if constexpr (Hash) fp.byte(ch);

            const bool stop =
                (lo == hi) ||
                (route == Route::Interval && bucket(lo, groups) == bucket(hi, groups)) ||
                (route == Route::Residual && static_cast<unsigned __int128>(hi - lo) * groups < Q);
            if (stop) { ++p; break; }
        }
        // EOS has cumulative 0, so the final coordinate is the current lower bound.
        return {bucket(lo, groups), static_cast<std::uint32_t>(p)};
    }
};

// ---------------------------------------------------------------------------
// On-block layout: one block per group, 64B-aligned. The suffix arena grows
// downward from the block end; tags sit at offset 16 (16B-aligned for SIMD).
// ---------------------------------------------------------------------------

struct Entry {
    std::uint32_t offset;
    std::uint32_t length;
    std::uint64_t value;
};

// E5-1: tags[48] keeps the header at 64B for every supported slot count (S<=48);
// tags start at offset 16, so every 16-tag chunk is 16-byte aligned for SIMD loads.
struct alignas(64) Header {
    std::uint64_t overflow_pointer;
    std::uint16_t count, bottom, flags, reserved0;
    std::uint8_t tags[48];
};

static_assert(sizeof(Entry) == 16 && sizeof(Header) == 64);
static_assert(sizeof(std::uintptr_t) == 8, "This validation layout assumes 64-bit pointers");

struct GroupStats {
    std::uint32_t group = 0;
    std::size_t primary_entries = 0, primary_bytes = 0, overflow_entries = 0, overflow_bytes = 0;
    std::size_t free_entries = 0, free_bytes = 0, garbage_bytes = 0;
    std::size_t blocked_slots_only = 0, blocked_bytes_only = 0, blocked_both = 0, oversized = 0, fits_now = 0;
};

// ---------------------------------------------------------------------------
// E20 (R2): what the statistics stage hands to the final construction.
//
// Carrying `group_ids` across the two stages is only sound if both map a string to
// the SAME logical group. That needs the prefix and the logical group count to
// agree, so they travel with the ids and the constructor checks them. Capacity
// tables and physical sub-block counts are deliberately NOT part of this: changing
// them keeps the group id, and the round states that distinction explicitly.
//
// The statistics stage's overflow membership is also deliberately not carried: it
// is a property of the PROBE's capacity, not of the final layout's.
// ---------------------------------------------------------------------------
struct RegionDemand {
    std::vector<GroupStats> stats;
    std::vector<std::uint32_t> group_ids;   // one per key, in input order
    std::string prefix;                     // the prefix the ids were computed under
    std::uint32_t groups = 0;               // the logical group count they were computed with
    // E21: identity of the string -> group mapping. Prefix and group count alone
    // are not enough -- a different input, or the same input under a different
    // model kind, can share both while training to a different table. See
    // Region::model_identity.
    std::uint64_t model_digest = 0;
    // E21/E22: a HELPER check on the key sequence, NOT an identity for it.
    //
    // Equal model tables do not imply equal inputs, so a check on the sequence is
    // needed. This one is deliberately cheap: one FNV step per key over its length
    // and its two boundary bytes. That is enough to catch an accidental reorder or
    // a resized key, and NOT enough to establish that the sequence is unchanged --
    // "aaba" and "abba" have the same length, the same first byte and the same last
    // byte, and are indistinguishable here before any mixing happens. The source
    // address and length do not close that gap either: they cannot see an in-place
    // edit.
    //
    // The guarantee therefore does NOT rest on this value. See
    // Index::bulk_load_reusing_ids: the context is created and consumed inside one
    // call and never reaches a caller, so it cannot be stored and replayed against
    // different data. This digest only makes some misuse visible.
    std::uint64_t key_digest = 0;
};

// E21/E22: the whole context, including the identity of the key array it came from.
//
// INTERNAL. A context is produced and consumed inside Index::bulk_load_reusing_ids
// and must not be held, copied out, or replayed against other data; the lifetime is
// what makes the reuse sound. The source address and size are a checked
// precondition, not the guarantee.
struct BuildContext {
    const Pair* source = nullptr;
    std::size_t source_size = 0;
    std::vector<RegionDemand> regions;
};

// ---------------------------------------------------------------------------
// Region: one contiguous lexicographic interval = model + capacity table + blocks.
// ---------------------------------------------------------------------------

class Region {
    // E5-1: the entry-slot count S and the entries-area end are per-region values now.
    static constexpr std::size_t MAX_SLOTS = 48;
    static constexpr std::size_t NO_SUBBLOCK = ~std::size_t{0};   // E11 locate result

    struct OverflowRecord {
        std::string suffix;
        std::uint64_t value;
        std::uint8_t tag;
    };
    using Overflow = std::vector<OverflowRecord>;

    Config config_;

    // E8: per-group capacity table. Uniform builds fill every group with the same
    // (slots, block bytes); demand builds give crowded groups more of both.
    //
    // E9: a group owns subblocks(g) consecutive, EQUAL-SIZED sub-blocks, and the
    // tables below describe ONE sub-block rather than the whole group. The count
    // is derived from off_ and bb_, so multi-block layouts add no locator metadata
    // at all. nb == 1 reproduces the E8 behaviour exactly.
    std::vector<std::uint16_t> sg_;   // slots per sub-block (<= MAX_SLOTS)
    std::vector<std::uint32_t> bb_;   // bytes per sub-block (64-aligned)
    std::vector<std::size_t> off_;    // group start offsets, size groups_+1
    // E31H: per-group reclaim hysteresis state, sized like the other per-group tables and
    // allocated only when the switch is on, so a build with it off pays nothing. Remembering
    // per group the operation counter and the garbage at its last attempt is the checklist's
    // "record the layout version and the reason the last attempt failed" -- a repeat is
    // allowed only once the group's state has materially moved.
    std::vector<std::uint64_t> reclaim_last_op_;     // placements_since_ at the last attempt
    std::vector<std::size_t> reclaim_last_garbage_;  // garbage bytes at the last attempt
    std::uint64_t placements_since_ = 0;             // successful placements, the interval scale
    static constexpr std::uint64_t kNoReclaimAttempt = ~std::uint64_t(0);
    // 64 successful placements. E30 measured the reclaim pass at 6.1-11.4 ns/record against
    // 163 ns for the whole-group compaction it displaces, and E31 measured the trigger firing
    // 127,780-255,882 times at the 200% checkpoint while enabling only 20.4%-67.2% of the
    // placements that raised it. Bounding a group's repeat attempts to one per 64 writes is
    // small next to a group's own live count (up to MAX_SLOTS records), so a group whose
    // layout did change is retried long before it could matter, while one whose layout did
    // not is not retried at all.
    static constexpr std::uint64_t kMinReclaimGap = 64;

    std::string prefix_;
    Model model_;
    std::byte* storage_ = nullptr;
    std::uint32_t groups_ = 1;
    std::size_t size_ = 0, overflow_count_ = 0;
    std::size_t compactions = 0;   // E9 gate instrumentation: cross-sub-block repacks
    std::vector<std::unique_ptr<Overflow>> overflow_owners_;

    // E11 boundary directory. One entry per NON-EMPTY sub-block of a multi-block
    // group: the sub-block's minimum suffix and its index, in sub-block order.
    // Boundaries own their bytes (a copy, not a view into a block) so compaction
    // can never leave a dangling reference. Single-block groups keep this empty
    // and pay nothing but the outer vector slot.
    struct Boundary {
        std::string key;
        std::uint32_t subblock = 0;
    };
    std::vector<std::vector<Boundary>> dirs_;
    std::size_t dir_rebuilds = 0, dir_entries_built = 0;
    bool suppress_dir_rebuild = false;   // set while compact() replays a whole group

    // --- per-group geometry ---

    std::size_t slots_of(std::uint32_t g) const { return sg_[g]; }

    std::size_t start_of(std::uint32_t g) const {
        return sizeof(Header) + std::size_t(sg_[g]) * sizeof(Entry);
    }

    // E9: sub-blocks of a group are consecutive and equal-sized, so their number
    // and base addresses follow from the two existing tables. No new metadata.
    std::size_t subblocks(std::uint32_t g) const {
        return (off_[g + 1] - off_[g]) / bb_[g];
    }

    std::size_t subblock_offset(std::uint32_t g, std::size_t i) const {
        return off_[g] + i * bb_[g];
    }

    std::byte* base_of(std::uint32_t g, std::size_t i) {
        return config_.offset_directory
            ? storage_ + subblock_offset(g, i)
            : storage_ + std::size_t(g) * config_.block_bytes;
    }

    const std::byte* base_of(std::uint32_t g, std::size_t i) const {
        return config_.offset_directory
            ? storage_ + subblock_offset(g, i)
            : storage_ + std::size_t(g) * config_.block_bytes;
    }

    Header& header(std::uint32_t g, std::size_t i) {
        return *reinterpret_cast<Header*>(base_of(g, i));
    }
    const Header& header(std::uint32_t g, std::size_t i) const {
        return *reinterpret_cast<const Header*>(base_of(g, i));
    }

    Entry* entries(std::uint32_t g, std::size_t i) {
        return reinterpret_cast<Entry*>(base_of(g, i) + sizeof(Header));
    }
    const Entry* entries(std::uint32_t g, std::size_t i) const {
        return reinterpret_cast<const Entry*>(base_of(g, i) + sizeof(Header));
    }

    // The overflow container belongs to the logical group and is anchored in
    // sub-block 0; the other sub-blocks leave overflow_pointer at zero.
    Overflow* overflow(std::uint32_t g) const {
        return reinterpret_cast<Overflow*>(static_cast<std::uintptr_t>(header(g, 0).overflow_pointer));
    }

    std::string_view suffix(std::uint32_t g, std::size_t i, const Entry& e) const {
        assert(e.offset >= start_of(g) && std::uint64_t(e.offset) + e.length <= bb_[g]);
        return {reinterpret_cast<const char*>(base_of(g, i) + e.offset), e.length};
    }

    bool prefix_matches(std::string_view s) const {
        return s.size() >= prefix_.size() &&
               (!prefix_.size() || std::memcmp(s.data(), prefix_.data(), prefix_.size()) == 0);
    }

    std::uint32_t canonical_group(std::string_view s) const {
        E18_INC(group_calls);
        Fingerprint dummy;
        return model_.predict<false>(s, groups_, config_.canonical, dummy).group;
    }

    // --- E11 boundary directory ---

    // Rebuild one group's directory from its current sub-blocks. Called whenever a
    // sub-block's minimum key can have changed: a front insert, a front erase, or a
    // repack. O(sub-blocks), and only for multi-block groups.
    void rebuild_dir(std::uint32_t g) {
        if (dirs_.empty()) return;

        auto& dir = dirs_[g];
        dir.clear();

        const std::size_t nb = subblocks(g);
        if (nb < 2) return;   // single-block groups keep no directory

        ++dir_rebuilds;
        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);
            if (!h.count) continue;
            dir.push_back({std::string(suffix(g, i, entries(g, i)[0])),
                           static_cast<std::uint32_t>(i)});
            ++dir_entries_built;
        }
    }

    void rebuild_all_dirs() {
        for (std::uint32_t g = 0; g < groups_; ++g) rebuild_dir(g);
    }

    // The sub-block that can hold s, or NO_SUBBLOCK when s sorts below every
    // primary key. M1 makes this the ONLY candidate: everything in an earlier
    // sub-block is smaller than that sub-block's minimum, and everything in a
    // later one is larger. NOT finding s here says nothing about the overflow
    // container -- the caller must still search it.
    std::size_t locate_subblock(std::uint32_t g, std::string_view s, Result& r) const {
        const auto& dir = dirs_[g];
        std::size_t lo = 0, hi = dir.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            E12_TICK(r.locate_probes);
#ifdef E7_COUNT_BYTES
            const int c = compare_bytes(dir[mid].key, s, r.locate_cmp_bytes);
#else
            const int c = compare(dir[mid].key, s);
#endif
            if (c <= 0) lo = mid + 1;
            else hi = mid;
        }
        return lo == 0 ? NO_SUBBLOCK : dir[lo - 1].subblock;
    }

    // --- E16: derivations shared by the constructor and the statistics-only path
    //
    // Extracted verbatim from the constructor so the statistics-only path below
    // uses the SAME formulas instead of a copy of them. Each is exactly the
    // expression the constructor used inline, in the same order. check_e16_probe
    // compares the two paths field by field over many configurations, so a
    // divergence introduced here is caught rather than silently absorbed.
    //
    // They take a [first, last) range rather than a vector so a region's slice can
    // be addressed IN PLACE. Materialising that slice as a vector<Pair> is itself
    // measurable work (E14/E15 §3b), and the whole point of this path is to avoid
    // work the probe was doing.
    // -----------------------------------------------------------------------

    static std::string compute_prefix(const Pair* first, const Pair* last) {
        if (first == last) return {};
        const auto& a = first->first;
        const auto& b = (last - 1)->first;
        std::size_t p = 0;
        while (p < std::min(a.size(), b.size()) && a[p] == b[p]) ++p;
        return a.substr(0, p);
    }

    static std::uint64_t suffix_bytes_of(const Pair* first, const Pair* last, std::size_t prefix_len) {
        std::uint64_t n = 0;
        for (const Pair* it = first; it != last; ++it) n += it->first.size() - prefix_len;
        return n;
    }

    // E21: an identity for the string -> group mapping, without predicting anything.
    //
    // The mapping is (prefix, trained table, logical group count, routing mode).
    // Folding all four into one value means a single comparison decides whether a
    // context may be reused -- and it catches the two cases prefix+count cannot:
    // a different key set, and the same keys under a different model kind. If the
    // digest matches, the predictions are equal by construction.
    //
    // Cost is a pass over the trained table (16 x 258 u32 = 16.5 KiB per region),
    // and it is only paid when a preset is actually offered.
    static std::uint64_t model_identity(const Model& m, std::uint32_t groups, Route canonical) {
        std::uint64_t h = 1469598103934665603ULL;
        // One FNV step per BYTE. An earlier version stepped once per 8-byte value,
        // which cost eight multiplies per table entry -- about 3 ms per build across
        // two passes, i.e. the whole of what R2 saved. Byte-wise is the same hash
        // strength at roughly an eighth of the cost.
        auto mix_bytes = [&h](const void* p, std::size_t n) {
            const auto* b = static_cast<const unsigned char*>(p);
            for (std::size_t i = 0; i < n; ++i) {
                h ^= b[i];
                h *= 1099511628211ULL;
            }
        };
        const std::uint64_t kind = static_cast<std::uint64_t>(m.kind);
        const std::uint64_t g = groups;
        const std::uint64_t c = static_cast<std::uint64_t>(canonical);
        mix_bytes(&kind, sizeof(kind));
        mix_bytes(&g, sizeof(g));
        mix_bytes(&c, sizeof(c));
        for (const auto& row : m.cumulative)
            mix_bytes(row.data(), row.size() * sizeof(std::uint32_t));
        return h;
    }

    static std::uint32_t derive_groups(std::size_t n, std::uint64_t suffix_bytes, const Config& cfg) {
        const double count_groups = n / (double(cfg.slots) * cfg.load);
        const double byte_groups = suffix_bytes /
            ((cfg.block_bytes - sizeof(Header) - std::size_t(cfg.slots) * sizeof(Entry)) * cfg.load);
        const auto estimate = static_cast<std::uint64_t>(std::max(count_groups, byte_groups)) + 1;
        if (estimate > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("too many groups");
        return cfg.fixed_groups
            ? cfg.fixed_groups
            : static_cast<std::uint32_t>(std::max<std::uint64_t>(1, estimate));
    }

    // --- write paths ---

    // Insert into one sub-block at its sorted position; false if it has no room.
    bool insert_into(std::uint32_t g, std::size_t i, std::string_view s,
                     std::uint64_t value, std::uint8_t tag) {
        E18_INC(insert_attempts);
        E25_INC(place_attempts);
        auto& h = header(g, i);
        // The two capacity failures are counted apart: "no free slot" and "not enough
        // suffix bytes" call for different remedies, and a combined counter cannot
        // tell them apart.
        if (h.count >= slots_of(g)) { E25_INC(place_fail_slots); return false; }
        if (s.size() > h.bottom - start_of(g)) { E25_INC(place_fail_bytes); return false; }
        E18_INC(insert_ok);
        E18_ADD(suffix_bytes_copied, s.size());

        auto* es = entries(g, i);
        std::size_t p = 0;
        while (p < h.count && E25_CMP(suffix(g, i, es[p]), s) < 0) ++p;
        E18_ADD(entry_shifts, h.count - p);
        for (std::size_t j = h.count; j > p; --j) {
            es[j] = es[j - 1];
            h.tags[j] = h.tags[j - 1];
        }
        h.bottom = static_cast<std::uint16_t>(h.bottom - s.size());
        if (!s.empty()) std::memcpy(base_of(g, i) + h.bottom, s.data(), s.size());
        es[p] = {h.bottom, static_cast<std::uint32_t>(s.size()), value};
        h.tags[p] = tag;
        ++h.count;

        // A front insert becomes this sub-block's new minimum.
        if (p == 0 && !suppress_dir_rebuild) rebuild_dir(g);
        return true;
    }

    // E9: a group's sub-blocks are rank-ordered, so every key in sub-block i is
    // smaller than every key in sub-block i+1 (invariant M1, which range scan
    // relies on). Placement below keeps M1 without ever shifting across blocks:
    // s goes into the first sub-block whose upper bound is >= s, or into the gap
    // just before it when that sub-block is full, or after every existing key.
    // E31: which sub-block would put_primary try FIRST for this key? Factored out of
    // put_primary's own enumeration so the reclaim and the placement cannot drift apart.
    std::size_t first_candidate(std::uint32_t g, std::string_view s) const {
        const std::size_t nb = subblocks(g);
        std::size_t prev = SIZE_MAX;
        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);
            if (h.count == 0) continue;
            const auto* es = entries(g, i);
            if (compare(suffix(g, i, es[h.count - 1]), s) < 0) { prev = i; continue; }
            if (prev != SIZE_MAX && compare(s, suffix(g, i, es[0])) < 0) return prev;
            return i;
        }
        return prev == SIZE_MAX ? 0 : prev;
    }

    // E31: pack this sub-block's live suffixes toward the block end, in place.
    //
    // Entries are processed in DECREASING offset order. That ordering is what makes a
    // single buffer safe: each record moves to a destination above the region still
    // holding un-read records, and memmove covers the self-overlap. Descriptors do not
    // move (only their offsets change), nothing is allocated, no hash is recomputed, and
    // the boundary directory is untouched because the sub-block's key range is
    // unchanged -- its first and last entries keep their slots.
    void repack_subblock(std::uint32_t g, std::size_t blk) {
        auto& h = header(g, blk);
        const std::size_t n = h.count;
        if (n < 2) return;                      // nothing to reclaim
        auto* es = entries(g, blk);
        std::byte* base = base_of(g, blk);

        std::size_t order[48];
        for (std::size_t j = 0; j < n; ++j) order[j] = j;
        for (std::size_t a = 1; a < n; ++a) {
            const std::size_t key = order[a];
            const std::uint32_t koff = es[key].offset;
            std::size_t b = a;
            while (b > 0 && es[order[b - 1]].offset < koff) { order[b] = order[b - 1]; --b; }
            order[b] = key;
        }

        std::size_t top = bb_[g];
        for (std::size_t a = 0; a < n; ++a) {
            Entry& e = es[order[a]];
            top -= e.length;
            std::memmove(base + top, base + e.offset, e.length);
            e.offset = static_cast<std::uint32_t>(top);
        }
        h.bottom = static_cast<std::uint16_t>(top);
    }

    bool put_primary(std::uint32_t g, std::string_view s, std::uint64_t value, std::uint8_t tag) {
        const std::size_t nb = subblocks(g);
        std::size_t prev = SIZE_MAX;   // last non-empty sub-block whose max < s

        for (std::size_t i = 0; i < nb; ++i) {
            E18_INC(subblock_probes);
            const auto& h = header(g, i);
            if (h.count == 0) continue;

            auto* es = entries(g, i);
            if (compare(suffix(g, i, es[h.count - 1]), s) < 0) { prev = i; continue; }

            // s is at or below this sub-block's upper bound: it belongs here, or
            // in the gap immediately before it when it sorts below the first key.
            if (prev != SIZE_MAX && compare(s, suffix(g, i, es[0])) < 0 &&
                insert_into(g, prev, s, value, tag))
                return true;
            return insert_into(g, i, s, value, tag);
        }

        // Greater than every existing key: append to the first sub-block with room
        // at or after `prev`. Starting at `prev` itself is correct (appending there
        // keeps the rank order) and covers the single-sub-block case.
        for (std::size_t j = prev == SIZE_MAX ? 0 : prev; j < nb; ++j)
            if (insert_into(g, j, s, value, tag)) return true;
        return false;
    }

    void put_overflow(std::uint32_t g, std::string_view s, std::uint64_t value, std::uint8_t tag) {
        E18_INC(overflow_inserts);
        auto* o = overflow(g);
        if (!o) {
            E18_INC(overflow_containers);
            overflow_owners_.push_back(std::make_unique<Overflow>());
            o = overflow_owners_.back().get();
            header(g, 0).overflow_pointer = reinterpret_cast<std::uintptr_t>(o);
        }
#ifdef E30_TIME
        const auto ov_t0 = E30_T0();
#endif
        auto it = std::lower_bound(o->begin(), o->end(), s,
            [](const OverflowRecord& a, std::string_view b) { return E25_CMP(a.suffix, b) < 0; });
        // E31 item 3: what vector::insert is about to push up, in records AND in bytes,
        // read while the distance is still valid. Two integer ops, and only in the
        // counter build -- this is not on the flag-free timing path.
        E25_ADD(ov_shifted_records_insert, o->end() - it);
        E25_ADD(ov_shifted_bytes_insert, (o->end() - it) * sizeof(OverflowRecord));
        E25_INC(string_allocs);
        o->insert(it, {std::string(s), value, tag});
        ++overflow_count_;
#ifdef E30_TIME
        E30_ADD(ov_insert_ns, ov_t0);
        E30_INC(ov_insert_calls);
#endif
    }

    void compact(std::uint32_t g) {
        const auto e30_t0 = E30_T0();
        E30_INC(compact_calls);
        ++compactions;
        const std::size_t nb = subblocks(g);
        std::vector<Pair> live;
        // E26 C1. The tags are collected ONLY when they are going to be reused: making
        // the deferred version pay for a vector it does not read would tilt the A/B.
        const bool want_tags = config_.compact_reuse_tag;
        std::vector<std::uint8_t> live_tags;
        for (std::size_t i = 0; i < nb; ++i)
            live.reserve(live.size() + header(g, i).count);
        if (want_tags) live_tags.reserve(live.capacity());

        std::size_t garbage_before = 0, bytes_copied = 0;

        // Sub-blocks are rank-ordered, so collecting them in order yields the
        // group's keys already sorted; replaying them refills from sub-block 0.
        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);
            const auto* es = entries(g, i);
            std::size_t live_in_block = 0;
            for (std::size_t j = 0; j < h.count; ++j) {
                live_in_block += es[j].length;
                bytes_copied += es[j].length;
                live.emplace_back(suffix(g, i, es[j]), es[j].value);
                if (want_tags) live_tags.push_back(h.tags[j]);
            }
            // Occupied region is bb - bottom; whatever it holds beyond the live
            // suffixes is deleted bytes the block has not reclaimed.
            const std::size_t occupied = std::size_t(bb_[g]) - h.bottom;
            if (occupied > live_in_block) garbage_before += occupied - live_in_block;
        }
        E25_ADD(compact_garbage_before, garbage_before);
        E25_ADD(compact_records_replayed, live.size());
        E25_ADD(compact_bytes_copied, bytes_copied);
        E30_ADD(collect_ns, e30_t0);   // entry through the gather, before the re-fill

        for (std::size_t i = 0; i < nb; ++i) {
            header(g, i).count = 0;
            header(g, i).bottom = static_cast<std::uint16_t>(bb_[g]);
        }

        // The replay front-inserts into every sub-block; rebuilding the directory
        // on each of those would be pure waste, so it is rebuilt once at the end.
        suppress_dir_rebuild = true;
        for (std::size_t k = 0; k < live.size(); ++k) {
            std::uint8_t tg;
            if (want_tags) {
                tg = live_tags[k];
#ifdef E25_DIAG
                // The reuse is sound only if the stored tag still equals the
                // recomputed one. Checked in the DIAGNOSTIC build alone -- the timed
                // build must not pay for the very hashing this change removes.
                if (Fingerprint::of(live[k].first) != live_tags[k])
                    E25_INC(compact_tag_mismatch);
#endif
            } else {
                tg = Fingerprint::of(live[k].first);
            }
            bool ok = put_primary(g, live[k].first, live[k].second, tg);
            if (!ok) throw std::logic_error("compaction lost capacity");
        }
        suppress_dir_rebuild = false;
        rebuild_dir(g);
        E30_ADD(compact_ns, e30_t0);          // whole call, collect + replay + directory
    }

public:
    Region(const std::vector<Pair>& sorted, Config cfg) : Region(sorted, cfg, {}, {}, {}) {}

    // nb_cap_in is an optional per-group ceiling on the sub-block count, used by
    // the strict-budget allocator to keep an unfunded group on its single-block
    // layout. It is build-time configuration only: nothing is stored, and
    // subblocks(g) still derives from off_ and bb_.
    Region(const std::vector<Pair>& sorted, Config cfg,
           std::vector<std::uint16_t> sg_in, std::vector<std::uint32_t> bb_in,
           std::vector<std::uint16_t> nb_cap_in = {},
           const RegionDemand* preset = nullptr)
        : config_(cfg) {
        const auto e14_ctor_t0 = E14_T0();
        E14_INC(ctor_calls);
        E18_INC(region_ctors);
        if (cfg.block_bytes > 65535 || cfg.block_bytes % 64 || cfg.load <= 0 || cfg.load >= 1)
            throw std::invalid_argument("layout config");

        {
            const auto t0 = E14_T0();
            prefix_ = compute_prefix(sorted.data(), sorted.data() + sorted.size());
            E14_ADD(prefix, t0);
        }

        {
            const auto t0 = E14_T0();
            model_.train(sorted, prefix_.size(), config_.model_kind);
            E14_ADD(model, t0);
        }

        std::uint64_t suffix_bytes = 0;
        {
            const auto t0 = E14_T0();
            suffix_bytes = suffix_bytes_of(sorted.data(), sorted.data() + sorted.size(), prefix_.size());
            E14_ADD(suffix_scan, t0);
        }

        // All suffix bytes count in this average-capacity estimate, including long ones.
        //
        // E14: `layout` is accumulated in TWO segments -- before and after the E9
        // demand pass -- so that it EXCLUDES the demand pass. Measuring it as one
        // span would count the demand pass twice and the phases would then sum to
        // more than ctor_total. With the split, the eight phases partition the
        // constructor and their sum reconciles with ctor_total.
        const auto e14_layout_t0 = E14_T0();
        groups_ = derive_groups(sorted.size(), suffix_bytes, cfg);

        // E8: per-group capacity table (uniform default or explicit demand table)
        if (sg_in.empty()) {
            sg_in.assign(groups_, static_cast<std::uint16_t>(cfg.slots));
            bb_in.assign(groups_, cfg.block_bytes);
        }
        if (sg_in.size() != groups_ || bb_in.size() != groups_)
            throw std::invalid_argument("capacity table size");
        sg_ = std::move(sg_in);
        bb_ = std::move(bb_in);
        // E31H: hysteresis state, allocated only when the switch asks for it.
        if (reclaim_gated()) {
            reclaim_last_op_.assign(groups_, kNoReclaimAttempt);
            reclaim_last_garbage_.assign(groups_, 0);
        }
        E14_ADD(layout, e14_layout_t0);   // segment A: group estimate + capacity table

        // E9 pass 1: each group's real demand (entries and suffix bytes) decides
        // how many equal-sized sub-blocks it needs. Both constraints must hold,
        // and the resulting count stays derivable from off_ and bb_ alone. This
        // pass runs only in multi-block mode; otherwise every group has nb == 1.
        std::vector<std::uint32_t> entries_need;
        std::vector<std::uint64_t> bytes_need;
        // E19 (R1): the group id of every key in this region, recorded by the
        // demand pass and read back by the placement loop. Sized to the region
        // (region_keys entries at most), so the peak is a few KiB, not the whole
        // key set. Empty when the demand pass does not run.
        std::vector<std::uint32_t> group_of;
        // E20 (R2): accept the preset only if this region's own prefix and logical
        // group count match the ones the ids were computed under. A length check
        // alone would not catch a changed prefix or group count, both of which
        // change the mapping while leaving the count of ids equal.
        // E21: the model digest is checked LAST and only when a preset is offered,
        // so the default and R1 paths never pay for it.
        const bool preset_ok = preset != nullptr && preset->group_ids.size() == sorted.size() &&
                               preset->prefix == prefix_ && preset->groups == groups_ &&
                               preset->model_digest ==
                                   model_identity(model_, groups_, config_.canonical);
        if (cfg.multiblock) {
            const auto t0 = E14_T0();
            entries_need.assign(groups_, 0);
            bytes_need.assign(groups_, 0);
            if (config_.reuse_group_ids && !preset_ok) group_of.resize(sorted.size());
            for (std::size_t i = 0; i < sorted.size(); ++i) {
                auto s = std::string_view(sorted[i].first).substr(prefix_.size());
                const auto g = preset_ok ? preset->group_ids[i] : canonical_group(s);
                if (!group_of.empty()) group_of[i] = g;
                ++entries_need[g];
                bytes_need[g] += s.size();
            }
            E14_ADD(demand_pass, t0);
        }
        // The reuse is conditional on the recorded ids covering exactly this key
        // sequence. Anything else falls back to computing them, so a future change
        // that breaks the correspondence degrades to the old behaviour instead of
        // silently misplacing records.
        const bool reuse_ids = cfg.multiblock && config_.reuse_group_ids &&
                               group_of.size() == sorted.size();

        const auto e14_layout2_t0 = E14_T0();
        off_.assign(std::size_t(groups_) + 1, 0);
        for (std::uint32_t g = 0; g < groups_; ++g) {
            const auto so = sizeof(Header) + std::size_t(sg_[g]) * sizeof(Entry);
            if (sg_[g] == 0 || sg_[g] > MAX_SLOTS || bb_[g] % 64 || bb_[g] <= so || bb_[g] > 65535)
                throw std::invalid_argument("layout config");

            std::uint64_t nb = 1;
            if (cfg.multiblock) {
                if (!nb_cap_in.empty() && nb_cap_in.size() != groups_)
                    throw std::invalid_argument("sub-block cap size");

                const std::uint64_t per_block = bb_[g] - so;
                nb = (entries_need[g] + sg_[g] - 1) / sg_[g];
                nb = std::max<std::uint64_t>(nb, (bytes_need[g] + per_block - 1) / per_block);
                nb = std::max<std::uint64_t>(nb, 1);

                // 0 means uncapped; any positive value is the ceiling. The strict
                // allocator uses 1 to pin an unfunded group to a single block.
                if (!nb_cap_in.empty() && nb_cap_in[g] != 0)
                    nb = std::min<std::uint64_t>(nb, nb_cap_in[g]);

                if (nb > 65535) throw std::length_error("too many sub-blocks");
            }
            off_[g + 1] = off_[g] + std::size_t(nb) * bb_[g];
        }

        // Legacy fixed-stride addressing cannot express more than one block per
        // group, so a multi-block layout requires the offset directory.
        if (!config_.offset_directory)
            for (std::uint32_t g = 0; g < groups_; ++g)
                if (subblocks(g) != 1)
                    throw std::invalid_argument("fixed stride requires one sub-block per group");
        E14_ADD(layout, e14_layout2_t0);   // segment B: offset derivation

        // E14 note: `storage` covers the storage allocation, the header/entry
        // initialisation AND the per-group outer directory slots -- all three are
        // one pass over `groups_`/`subblocks`.
        const auto e14_storage_t0 = E14_T0();
        storage_ = static_cast<std::byte*>(::operator new(off_.back(), std::align_val_t{64}));
        for (std::uint32_t g = 0; g < groups_; ++g) {
            for (std::size_t i = 0, nb = subblocks(g); i < nb; ++i) {
                new (base_of(g, i)) Header{};
                header(g, i).bottom = static_cast<std::uint16_t>(bb_[g]);
                for (std::size_t j = 0; j < sg_[g]; ++j) new (entries(g, i) + j) Entry{};
            }
        }

        // Only multi-block layouts carry a boundary directory; a single-block
        // layout must not pay even the outer vector slots.
        if (cfg.multiblock) dirs_.assign(groups_, {});
        E14_ADD(storage, e14_storage_t0);

        suppress_dir_rebuild = true;
#ifdef E18_PREPASS
        // Diagnostic variant: compute every group id and fingerprint FIRST, into
        // one pass, then run the placement loop over the prepared arrays. The two
        // spans partition the placement work with TWO clock calls per region
        // instead of per key. canonical_group and Fingerprint::of are pure, so the
        // placement decisions -- and the resulting structure -- are unchanged; the
        // round verifies that by digest.
        std::vector<std::uint32_t> pre_g(sorted.size());
        std::vector<std::uint8_t> pre_tag(sorted.size());
        {
            const auto t0 = E14_T0();
            for (std::size_t i = 0; i < sorted.size(); ++i) {
                auto s = std::string_view(sorted[i].first).substr(prefix_.size());
                pre_g[i] = reuse_ids ? group_of[i] : canonical_group(s);
                E18_INC(fingerprint_calls);
                pre_tag[i] = static_cast<std::uint8_t>(Fingerprint::of(s));
            }
            E14_ADD(grp_prep, t0);
        }
        const auto e14_place_t0 = E14_T0();
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            auto s = std::string_view(sorted[i].first).substr(prefix_.size());
            if (!put_primary(pre_g[i], s, sorted[i].second, pre_tag[i]))
                put_overflow(pre_g[i], s, sorted[i].second, pre_tag[i]);
            ++size_;
        }
        suppress_dir_rebuild = false;
        E14_ADD(place, e14_place_t0);
#else
        const auto e14_place_t0 = E14_T0();
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const auto& kv = sorted[i];
            auto s = std::string_view(kv.first).substr(prefix_.size());
            // E19 (R1): read the id the demand pass computed rather than asking
            // the model the same question again.
            const auto g = preset_ok ? preset->group_ids[i]
                                     : (reuse_ids ? group_of[i] : canonical_group(s));
            E18_INC(fingerprint_calls);
            const auto tag = Fingerprint::of(s);
            if (!put_primary(g, s, kv.second, static_cast<std::uint8_t>(tag)))
                put_overflow(g, s, kv.second, static_cast<std::uint8_t>(tag));
            ++size_;
        }
        suppress_dir_rebuild = false;
        E14_ADD(place, e14_place_t0);
#endif

        const auto e14_dir_t0 = E14_T0();
        rebuild_all_dirs();
        E14_ADD(dir, e14_dir_t0);

        E14_ADD(ctor_total, e14_ctor_t0);
    }

    ~Region() { ::operator delete(storage_, std::align_val_t{64}); }
    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;

    // --- read-only accessors ---

    std::size_t size() const { return size_; }
    std::size_t overflow_count() const { return overflow_count_; }
    // E9 gate: lets a test assert that cross-sub-block compaction really ran.
    std::size_t compaction_count() const { return compactions; }
    std::size_t prefix_length() const { return prefix_.size(); }
    const std::string& prefix() const { return prefix_; }
    std::uint32_t groups() const { return groups_; }
    // E33/SA0: read-only exposure of the group mapping, next to the other read-only
    // accessors (slot_table / block_table / boundary_dir / subblock_keys) that exist so a
    // diagnostic can CHECK the mapping instead of reading the code and hoping. The
    // structure review asks for one trace from a real key to its group and for I1
    // (the query group equals the placement group) to be verified, and both need this
    // callable from outside without making the placement path itself public.
    std::uint32_t group_of(std::string_view suffix) const { return canonical_group(suffix); }

    std::size_t slots() const { return sg_.empty() ? 0 : sg_[0]; }

    std::size_t block_bytes_total() const { return off_.back(); }

    // E8-6: read-only exposure of the capacity tables for structural validation.
    const std::vector<std::uint16_t>& slot_table() const { return sg_; }
    const std::vector<std::uint32_t>& block_table() const { return bb_; }
    const std::vector<std::size_t>& offset_table() const { return off_; }

    // E9: the sub-block count is derived, not stored, so there is no table to
    // expose — the gate recomputes it from off_/bb_ and compares.
    std::size_t subblock_count(std::uint32_t g) const { return subblocks(g); }

    // E11: whether this group has a boundary directory (only multi-block groups do),
    // and the directory's contents for the targeted differential.
    bool has_dir(std::uint32_t g) const { return !dirs_.empty() && !dirs_[g].empty(); }

    std::vector<std::pair<std::string, std::uint32_t>> boundary_dir(std::uint32_t g) const {
        std::vector<std::pair<std::string, std::uint32_t>> out;
        if (dirs_.empty()) return out;
        for (const auto& b : dirs_[g]) out.emplace_back(b.key, b.subblock);
        return out;
    }

    std::size_t directory_rebuilds() const { return dir_rebuilds; }
    std::size_t directory_entries_built() const { return dir_entries_built; }

    std::size_t subblock_entries(std::uint32_t g, std::size_t i) const {
        return header(g, i).count;
    }

    // Keys of one sub-block in stored order, as suffixes (all sub-blocks of a
    // group share the region prefix, so suffixes compare like full keys).
    std::vector<std::string> subblock_keys(std::uint32_t g, std::size_t i) const {
        std::vector<std::string> out;
        const auto& h = header(g, i);
        const auto* es = entries(g, i);
        for (std::size_t j = 0; j < h.count; ++j) out.emplace_back(suffix(g, i, es[j]));
        return out;
    }

    struct SubBlockStats {
        std::size_t entries, bytes, free_bytes, garbage_bytes;
    };

    SubBlockStats subblock_stats(std::uint32_t g, std::size_t i) const {
        const auto& h = header(g, i);
        const auto* es = entries(g, i);
        SubBlockStats s{};
        s.entries = h.count;
        for (std::size_t j = 0; j < h.count; ++j) s.bytes += es[j].length;
        s.free_bytes = h.bottom - start_of(g);
        s.garbage_bytes = bb_[g] - h.bottom - s.bytes;
        return s;
    }

    struct MemBreakdown {
        std::size_t blocks, model, prefix, overflow, tables, directory, self;
    };

    MemBreakdown memory_breakdown() const {
        MemBreakdown m{};
        // E9: bb_ describes one sub-block, so the group's bytes come from off_.
        m.blocks = off_.back();
        m.model = model_.allocated_bytes();
        m.prefix = prefix_.capacity() + 1;
        m.overflow = overflow_owners_.capacity() * sizeof(void*);
        for (const auto& o : overflow_owners_) {
            m.overflow += sizeof(Overflow) + o->capacity() * sizeof(OverflowRecord);
            for (const auto& e : *o) m.overflow += e.suffix.capacity() + 1;
        }
        m.tables = off_.capacity() * sizeof(std::size_t) +
                   sg_.capacity() * sizeof(std::uint16_t) +
                   bb_.capacity() * sizeof(std::uint32_t);
        // E11: the boundary directory is real memory -- the outer slots for every
        // group, plus each boundary's own key buffer. It is NOT free metadata.
        m.directory = dirs_.capacity() * sizeof(std::vector<Boundary>);
        for (const auto& d : dirs_) {
            m.directory += d.capacity() * sizeof(Boundary);
            for (const auto& b : d) m.directory += b.key.capacity() + 1;
        }
        // E31H: the hysteresis tables are real memory when they are allocated.
        m.tables += reclaim_last_op_.capacity() * sizeof(std::uint64_t) +
                    reclaim_last_garbage_.capacity() * sizeof(std::size_t);
        m.self = sizeof(*this);
        return m;
    }

    const Model& model() const { return model_; }
    std::string_view residual(std::string_view key) const { return key.substr(prefix_.size()); }
    bool accepts_prefix(std::string_view key) const { return prefix_matches(key); }

    // E8-12: per-record overflow suffix lengths (E9 feasibility diagnostics).
    std::vector<std::uint32_t> overflow_lengths(std::uint32_t g) const {
        std::vector<std::uint32_t> out;
        if (auto* o = overflow(g))
            for (const auto& e : *o) out.push_back(static_cast<std::uint32_t>(e.suffix.size()));
        return out;
    }

    // E11 gate: the overflow suffixes of one group, so a test can query records
    // that live outside every primary block.
    std::vector<std::string> overflow_keys(std::uint32_t g) const {
        std::vector<std::string> out;
        if (auto* o = overflow(g))
            for (const auto& e : *o) out.emplace_back(e.suffix);
        return out;
    }

    std::size_t memory() const {
        // E8-6 fix: the offset/slot/block-size tables are now counted (they were
        // missing before; the omission is what the space-decomposition audit found).
        // E11 adds the boundary directory, which is likewise counted, not assumed away.
        const auto mb = memory_breakdown();
        return mb.self + mb.model + mb.prefix + mb.overflow + mb.blocks + mb.tables + mb.directory;
    }

    std::vector<GroupStats> group_stats() const {
        std::vector<GroupStats> out;
        out.reserve(groups_);

        for (std::uint32_t g = 0; g < groups_; ++g) {
            GroupStats s;
            s.group = g;

            // E9: aggregate the sub-blocks so the per-group totals keep the same
            // meaning they had with a single block (free/garbage are sums).
            for (std::size_t i = 0, nb = subblocks(g); i < nb; ++i) {
                const auto& h = header(g, i);
                const auto* es = entries(g, i);
                s.primary_entries += h.count;
                for (std::size_t j = 0; j < h.count; ++j) s.primary_bytes += es[j].length;
                s.free_entries += slots_of(g) - h.count;
                s.free_bytes += h.bottom - start_of(g);
                s.garbage_bytes += bb_[g] - h.bottom;
            }
            s.garbage_bytes -= s.primary_bytes;

            if (const auto* o = overflow(g)) {
                for (const auto& e : *o) {
                    ++s.overflow_entries;
                    s.overflow_bytes += e.suffix.size();
                    const bool slots = s.free_entries == 0, bytes = e.suffix.size() > s.free_bytes;
                    if (slots && bytes) ++s.blocked_both;
                    else if (slots) ++s.blocked_slots_only;
                    else if (bytes) ++s.blocked_bytes_only;
                    else ++s.fits_now;
                    if (e.suffix.size() > bb_[g] - start_of(g)) ++s.oversized;
                }
            }
            out.push_back(s);
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // E16: per-group statistics for a key range, WITHOUT constructing a Region.
    //
    // This is what the construction pipeline actually consumes. It builds a
    // "probe" Region per range purely to call group_stats(), and the two
    // capacity allocators read exactly four fields of each GroupStats
    // (primary_entries, primary_bytes, overflow_entries, overflow_bytes) --
    // E16's report carries the field-by-field audit of e9_alloc.hpp.
    //
    // The path replays the SAME acceptance test the single-block constructor runs
    // (insert_into's predicate) against the default capacity, in the same global
    // sorted order, keeping only a running (count, bottom) per group. It allocates
    // no block storage, copies no suffix into a block, and builds no overflow
    // container. The one per-key array is the group id, with the top bit recording
    // whether that key went to overflow, so the overflow-blocking partition can be
    // computed in a second pass without re-running the model.
    //
    // Every field of GroupStats is produced, not just the four the allocators read,
    // so this is a drop-in replacement and the gate can compare all of them.
    //
    // SCOPE: the single-block layout only, which is what the probe uses. A
    // multi-block config is REJECTED rather than approximated -- put_primary then
    // spans sub-blocks and the replay would have to model invariant M1 as well.
    // -----------------------------------------------------------------------
    // E21: public because Index::bulk_load_with_plan verifies a context with it.
    // E21: one FNV step per key over (length | first byte | last byte). O(1) per
    // key, so it stays far below the cost of the work it is protecting -- but it is
    // NOT free, and it is paid twice per build (once when the context is produced,
    // once when it is consumed). Hashing the full key bytes would cost more than
    // the optimisation it guards.
    static std::uint64_t key_sequence_digest(const Pair* first, const Pair* last) {
        std::uint64_t h = 1469598103934665603ULL;
        for (const Pair* it = first; it != last; ++it) {
            const auto& k = it->first;
            const std::uint64_t n = k.size();
            const std::uint64_t v =
                n | (std::uint64_t(n ? static_cast<unsigned char>(k.front()) : 0u) << 40) |
                (std::uint64_t(n ? static_cast<unsigned char>(k.back()) : 0u) << 48);
            h ^= v;
            h *= 1099511628211ULL;
        }
        return h;
    }

    static std::vector<GroupStats> demand_profile(const std::vector<Pair>& sorted, const Config& cfg) {
        return demand_profile(sorted.data(), sorted.data() + sorted.size(), cfg);
    }

    static std::vector<GroupStats> demand_profile(const Pair* first, const Pair* last, const Config& cfg) {
        // keep_ids = false: the statistics-only caller does not need the ids, and
        // building them here would make this path allocate an array per region that
        // it immediately discards -- which would also make it an unfair baseline for
        // the R2 comparison.
        //
        // std::move, not a plain member access: `demand_profile_impl(...)` is a
        // temporary, and `temp.stats` is an lvalue, so returning it by value would
        // COPY every region's statistics. That copy is invisible in the structure
        // and would only show up as a slower baseline.
        RegionDemand rd = demand_profile_impl(first, last, cfg, false);
        return std::move(rd.stats);
    }

    // E20 (R2): the same replay, but keeping the ids, the prefix and the logical
    // group count so the final construction can reuse them.
    static RegionDemand demand_profile_ctx(const Pair* first, const Pair* last, const Config& cfg) {
        return demand_profile_impl(first, last, cfg, true);
    }

    static RegionDemand demand_profile_impl(const Pair* first, const Pair* last, const Config& cfg,
                                            bool keep_ids) {
        if (cfg.multiblock)
            throw std::invalid_argument("E16: demand_profile covers the single-block layout only");
        if (cfg.block_bytes > 65535 || cfg.block_bytes % 64 || cfg.load <= 0 || cfg.load >= 1)
            throw std::invalid_argument("layout config");

        const std::size_t n = static_cast<std::size_t>(last - first);
        const std::string prefix = compute_prefix(first, last);
        const std::uint64_t sfx_bytes = suffix_bytes_of(first, last, prefix.size());
        const std::uint32_t groups = derive_groups(n, sfx_bytes, cfg);

        E18_ADD(group_calls, n);   // one prediction per key in this pass
        Model model;
        model.train(first, last, prefix.size(), cfg.model_kind);

        const std::size_t slots = cfg.slots;
        const std::size_t start = sizeof(Header) + slots * sizeof(Entry);
        const std::size_t bb = cfg.block_bytes;

        std::vector<GroupStats> out(groups);
        for (std::uint32_t g = 0; g < groups; ++g) out[g].group = g;

        std::vector<std::uint32_t> count(groups, 0);
        std::vector<std::uint32_t> bottom(groups, static_cast<std::uint32_t>(bb));
        // `where` carries the group id with the top bit marking "went to overflow".
        // The overflow bit is a property of the PROBE's capacity -- it must be
        // stripped before the ids are reused for placement.
        std::vector<std::uint32_t> where(n);
        constexpr std::uint32_t OVERFLOW_BIT = 0x80000000u;

        for (std::size_t i = 0; i < n; ++i) {
            const std::string_view s = std::string_view(first[i].first).substr(prefix.size());
            Fingerprint dummy;
            const std::uint32_t g = model.predict<false>(s, groups, cfg.canonical, dummy).group;
            where[i] = g;
            const std::size_t len = s.size();
            // insert_into's acceptance test, verbatim, for a single-block group.
            if (count[g] < slots && len <= std::size_t(bottom[g]) - start) {
                ++count[g];
                bottom[g] = static_cast<std::uint32_t>(bottom[g] - len);
                ++out[g].primary_entries;
                out[g].primary_bytes += len;
            } else {
                where[i] = g | OVERFLOW_BIT;
                ++out[g].overflow_entries;
                out[g].overflow_bytes += len;
            }
        }

        for (std::uint32_t g = 0; g < groups; ++g) {
            out[g].free_entries = slots - count[g];
            out[g].free_bytes = std::size_t(bottom[g]) - start;
            out[g].garbage_bytes = (bb - std::size_t(bottom[g])) - out[g].primary_bytes;
        }

        // Second pass: the overflow-blocking partition uses the FINAL primary
        // state, exactly as group_stats does.
        for (std::size_t i = 0; i < n; ++i) {
            if (!(where[i] & OVERFLOW_BIT)) continue;
            const std::uint32_t g = where[i] & ~OVERFLOW_BIT;
            const std::size_t len = first[i].first.size() - prefix.size();
            GroupStats& st = out[g];
            const bool no_slots = st.free_entries == 0, no_bytes = len > st.free_bytes;
            if (no_slots && no_bytes) ++st.blocked_both;
            else if (no_slots) ++st.blocked_slots_only;
            else if (no_bytes) ++st.blocked_bytes_only;
            else ++st.fits_now;
            if (len > bb - start) ++st.oversized;
        }

        RegionDemand rd;
        rd.prefix = prefix;
        rd.groups = groups;
        rd.model_digest = model_identity(model, groups, cfg.canonical);
        if (keep_ids) rd.key_digest = key_sequence_digest(first, last);
        rd.stats = std::move(out);
        if (keep_ids) {
            rd.group_ids.resize(n);
            for (std::size_t i = 0; i < n; ++i) rd.group_ids[i] = where[i] & ~OVERFLOW_BIT;
        }
        return rd;
    }

    // --- E25: located records and write helpers ---------------------------------
    //
    // Where a located record lives, and what the locate learned about the key. The
    // two halves answer different questions and the review asked for them to be
    // kept apart:
    //
    //   * the CONTEXT (group, tag, "the whole group was searched and the key is not
    //     here") is what a MISS needs, so the insert that follows does not predict
    //     the group and the fingerprint a second time;
    //   * the POSITION (sub-block + slot, or overflow index) is what a HIT needs,
    //     so update/erase write in place instead of searching again.
    //
    // VALIDITY: a Located is valid only inside the public operation that produced
    // it. Compaction, descriptor moves and container growth all invalidate the
    // position -- and the context too, if the group geometry or the model changed.
    // It is deliberately not a handle any caller can hold across operations.
    //
    // NotLocated vs Absent is load bearing: the first means no valid group id was
    // ever obtained (empty index, prefix mismatch), so there is nothing to reuse;
    // only the second means the legitimate group was searched exhaustively and the
    // key is genuinely absent, which is what makes it safe to place directly.
    enum class Where : std::uint8_t { NotLocated, Absent, Primary, Overflow };

    struct Located {
        Where where = Where::NotLocated;
        std::uint32_t group = 0;   // context: valid unless NotLocated
        std::uint8_t tag = 0;      // context: Fingerprint::of(s)
        std::size_t subblock = 0;  // position: Primary only
        std::size_t slot = 0;      // position: Primary only
        std::size_t index = 0;     // position: Overflow only
    };

    // E25 V1: the mode the write paths search with.
    Mode write_mode() const {
        return Mode{config_.canonical, Prefetch::Off, true, config_.write_simd,
                    OvSearch::Hybrid, config_.ov_threshold, config_.write_locate};
    }

    // Write the value of an already-located record, in place. No search.
    void store_at(const Located& loc, std::uint64_t value) {
        if (loc.where == Where::Primary)
            entries(loc.group, loc.subblock)[loc.slot].value = value;
        else if (loc.where == Where::Overflow)
            (*overflow(loc.group))[loc.index].value = value;
    }

    // Remove an already-located record: descriptors and tags shift down, the count
    // and the size drop, and a front removal rebuilds this group's boundary
    // directory because its minimum changed.
    bool erase_at(const Located& loc) {
        if (loc.where == Where::Primary) {
            auto& h = header(loc.group, loc.subblock);
            auto* es = entries(loc.group, loc.subblock);
            E25_ADD(erase_shifts, h.count - 1 - loc.slot);
            // The suffix bytes are NOT freed here -- the entry is dropped from the
            // descriptor array and its bytes stay in the block as garbage. That is
            // why this is counted apart from the overflow case.
            E25_ADD(erase_released_primary, es[loc.slot].length);
            for (std::size_t j = loc.slot + 1; j < h.count; ++j) {
                es[j - 1] = es[j];
                h.tags[j - 1] = h.tags[j];
            }
            --h.count;
            --size_;
            if (loc.slot == 0) rebuild_dir(loc.group);
            return true;
        }
        if (loc.where == Where::Overflow) {
            auto* o = overflow(loc.group);
            E25_ADD(erase_shifts, o->size() - 1 - loc.index);
            // E31 item 3: the same movement figure for the erase. The clock below covers
            // the vector::erase AND the destruction of the erased record -- a real free()
            // here, where a primary erase leaves its bytes in the block as garbage.
            E25_ADD(ov_shifted_records_erase, o->size() - 1 - loc.index);
            E25_ADD(ov_shifted_bytes_erase, (o->size() - 1 - loc.index) * sizeof(OverflowRecord));
            E25_ADD(erase_released_overflow, (*o)[loc.index].suffix.size());
            E25_INC(string_frees);   // the OverflowRecord, and with it its string, is destroyed
#ifdef E30_TIME
            const auto ov_t0 = E30_T0();
#endif
            o->erase(o->begin() + static_cast<std::ptrdiff_t>(loc.index));
#ifdef E30_TIME
            E30_ADD(ov_erase_ns, ov_t0);
            E30_INC(ov_erase_calls);
#endif
            --size_;
            --overflow_count_;
            return true;
        }
        return false;
    }

    // --- lookup ---

    template <bool Hash>
    Result find_impl(std::string_view key, Mode mode, Located* loc = nullptr) const {
        E25_INC(existence_searches);
        if (loc) *loc = Located{};   // NotLocated unless a valid group is reached below
        if (!size_ || !prefix_matches(key)) return {};
        if ((config_.canonical == Route::Residual) != (mode.route == Route::Residual))
            throw std::logic_error("incompatible mapping");

        const auto s = key.substr(prefix_.size());
        Fingerprint fp;
        const auto pred = model_.predict<Hash>(s, groups_, mode.route, fp);
        const auto g = pred.group;
        const std::byte* address = base_of(g, 0);

        if (mode.prefetch == Prefetch::Early) __builtin_prefetch(address, 0, 3);
        std::uint8_t tag = 0;
        if constexpr (Hash) {
            fp.tail(s.substr(pred.consumed));
            tag = fp.tag();
        }
        if (mode.prefetch == Prefetch::Late) __builtin_prefetch(address, 0, 3);

        // E25: the context half. From here the group id is valid, so a miss found
        // by the search below is genuinely "searched and absent".
        if (loc) {
            loc->where = Where::Absent;   // provisional; only kept if nothing is found
            loc->group = g;
            loc->tag = tag;
        }

        Result result;
        result.steps = pred.consumed;

        auto exact = [&](std::size_t blk, std::size_t i) {
            const auto* es = entries(g, blk);
            if (es[i].length == s.size()) {
                ++result.str_cmps;
                E12_TICK(result.str_cmps_primary);
                if (E25_CMP(suffix(g, blk, es[i]), s) == 0) {
                    result.found = true;
                    result.value = es[i].value;
                    result.source = 1;
                    if (loc) { loc->where = Where::Primary; loc->subblock = blk; loc->slot = i; }
                }
            }
        };

#if defined(__SSE2__)
        constexpr bool has_simd = true;
#else
        constexpr bool has_simd = false;
#endif

        // E9: a group may span several sub-blocks; each is tag-filtered in turn.
        // The tag filter keeps the extra blocks cheap (no full-key compares).
        //
        // E11: with the boundary directory in use, M1 makes the candidate sub-block
        // unique, so at most ONE block is visited. Not finding s there says nothing
        // about the overflow container -- M1 orders the primary sub-blocks among
        // themselves and says nothing about where overflow records sit -- so the
        // overflow search below runs unchanged on both paths.
        std::size_t lo_blk = 0, hi_blk = subblocks(g);
        if (mode.locate && has_dir(g)) {
            const std::size_t cand = locate_subblock(g, s, result);
            if (cand == NO_SUBBLOCK) { lo_blk = 0; hi_blk = 0; }   // below every primary key
            else { lo_blk = cand; hi_blk = cand + 1; }
        }

        for (std::size_t blk = lo_blk; blk < hi_blk && !result.found; ++blk) {
            const auto& h = header(g, blk);
            E12_TICK(result.subblocks_visited);   // header read even if the sub-block is empty
            E12_TICK(result.candidate_blocks);

            if constexpr (Hash && has_simd) {
                if (mode.simd) {
                    // E5-2: batched tag filter. Mask bits are clamped to live entries, so
                    // stale tag bytes beyond count can never produce a false candidate.
                    const __m128i vt = _mm_set1_epi8(static_cast<char>(tag));
                    for (std::size_t c = 0; c * 16 < slots_of(g); ++c) {
                        const std::size_t lo = c * 16;
                        const std::size_t top = std::min<std::size_t>(std::min(slots_of(g), lo + 16), h.count);
                        if (top <= lo) break;
                        E12_TICK(result.simd_batches);
                        const __m128i tv = _mm_load_si128(reinterpret_cast<const __m128i*>(h.tags + lo));
                        std::uint32_t mask = static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(tv, vt)));
                        mask &= (1u << unsigned(top - lo)) - 1u;
                        E12_ADD(result.tag_candidates, static_cast<std::uint32_t>(__builtin_popcount(mask)));
                        while (mask && !result.found) {
                            exact(blk, lo + unsigned(__builtin_ctz(mask)));
                            mask &= mask - 1;
                        }
                        if (result.found) break;
                    }
                    continue;
                }
            }
            if constexpr (Hash) {
                for (std::size_t i = 0; i < h.count && !result.found; ++i) {
                    if (h.tags[i] != tag) continue;
                    E12_TICK(result.tag_candidates);
                    exact(blk, i);
                }
            } else {
                for (std::size_t i = 0; i < h.count && !result.found; ++i) exact(blk, i);
            }
        }

        if (result.found) return result;

        if (auto* o = overflow(g)) {
            E12_TICK(result.overflow_entered);
            // E7: three lookup policies over the same sorted overflow vector.
            const bool use_bin = mode.ov == OvSearch::Binary ||
                (mode.ov == OvSearch::Hybrid && o->size() >= mode.ov_thr);

            if (use_bin) {
                std::size_t lo = 0, hi = o->size();
                while (lo < hi) {
                    const auto mid = lo + (hi - lo) / 2;
                    ++result.overflow_checks;
                    ++result.str_cmps;
                    E12_TICK(result.str_cmps_overflow);
                    if (E25_CMP((*o)[mid].suffix, s) < 0) lo = mid + 1;
                    else hi = mid;
                }
                if (lo < o->size()) {
                    ++result.str_cmps;
                    E12_TICK(result.str_cmps_overflow);
                    if (E25_CMP((*o)[lo].suffix, s) == 0) {
                        result.found = true;
                        result.value = (*o)[lo].value;
                        result.source = 2;
                        if (loc) { loc->where = Where::Overflow; loc->index = lo; }
                    }
                }
            } else {
                for (std::size_t oi = 0; oi < o->size(); ++oi) {
                    const auto& e = (*o)[oi];
                    ++result.overflow_checks;
                    if constexpr (Hash) {
                        if (e.tag != tag) continue;
                    }
                    ++result.str_cmps;
                    E12_TICK(result.str_cmps_overflow);
                    if (E25_CMP(e.suffix, s) == 0) {
                        result.found = true;
                        result.value = e.value;
                        result.source = 2;
                        if (loc) { loc->where = Where::Overflow; loc->index = oi; }
                        break;
                    }
                }
            }
        }
        return result;
    }

    Result find(std::string_view key, Mode mode) const {
        return mode.fingerprint ? find_impl<true>(key, mode) : find_impl<false>(key, mode);
    }

    // --- update paths ---

    // E9: whether any sub-block of the group could still take a record this long.
    // Used only to decide whether compaction is worth attempting before overflow.
    bool group_has_room(std::uint32_t g, std::size_t len) const {
        for (std::size_t i = 0, nb = subblocks(g); i < nb; ++i) {
            const auto& h = header(g, i);
            if (h.count < slots_of(g) && len <= h.bottom - start_of(g)) return true;
        }
        return false;
    }

    // E26 C2: un-referenced suffix bytes in this group -- the most a compaction could
    // reclaim. Occupied region minus live suffix bytes, summed over the sub-blocks.
    // O(entries), and evaluated only on a placement that ALREADY failed, so it is far
    // cheaper than the compaction it can skip.
    std::size_t group_garbage_bytes(std::uint32_t g) const {
        std::size_t total = 0;
        for (std::size_t i = 0, nb = subblocks(g); i < nb; ++i) {
            const auto& h = header(g, i);
            const auto* es = entries(g, i);
            std::size_t live = 0;
            for (std::size_t j = 0; j < h.count; ++j) live += es[j].length;
            const std::size_t occupied = std::size_t(bb_[g]) - h.bottom;
            if (occupied > live) total += occupied - live;
        }
        return total;
    }

    // E29 C3: could a full repack of this group hold one more record of length len?
    // A NECESSARY condition for compact() to succeed -- it replays every live record
    // and throws if the group cannot hold them.
    bool repack_budget_fits(std::uint32_t g, std::size_t len) const {
        std::size_t live = 0, free_slots = 0;
        const std::size_t nb = subblocks(g);
        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);
            const auto* es = entries(g, i);
            for (std::size_t j = 0; j < h.count; ++j) live += es[j].length;
            free_slots += slots_of(g) - h.count;
        }
        const std::size_t area = (std::size_t(bb_[g]) - start_of(g)) * nb;
        return free_slots > 0 && live + len <= area;
    }

    // E26 C2 / E29 C3: should a failed placement bother with a compaction?
    bool worth_compacting(std::uint32_t g, std::size_t len) const {
        if (group_has_room(g, len)) {                    // M0's own condition
            if (!config_.compact_needs_garbage) return true;
            return group_garbage_bytes(g) >= len;        // C2: can it free enough?
        }
        if (!config_.compact_repack_budget) return false;
        // C3: no single sub-block has both a slot and the bytes, but a repack of the
        // WHOLE group could still hold the record. Budget-guarded, because compact()
        // must not be run on a group that would not fit.
        return group_garbage_bytes(g) >= len && repack_budget_fits(g, len);
    }

    // E31H (checklist item 4.3): may the reclaim run for this group now? Off by default, in
    // which case this is a constant true and the E31 path is untouched.
    //
    // The checklist's rule is to set a minimum operation interval and not to retry while the
    // state has not materially changed. The two recorded quantities are the group's garbage at
    // its last attempt (the "layout version": a repack only helps again once new garbage has
    // accumulated) and the placement counter at that attempt (the interval). A first attempt
    // on a group is always allowed; a repeat needs BOTH a moved interval and grown garbage.
    // Each skip is counted separately, so the effect is countable rather than asserted.
    bool reclaim_gated() const {
        return config_.reclaim_hysteresis || config_.reclaim_skip_repeat;
    }

    bool reclaim_allowed(std::uint32_t g) {
        if (!reclaim_gated()) return true;
        const std::size_t garbage = group_garbage_bytes(g);
        const std::uint64_t last = reclaim_last_op_[g];
        if (last != kNoReclaimAttempt) {
            if (config_.reclaim_hysteresis &&
                placements_since_ - last < kMinReclaimGap) {
                E25_INC(reclaim_skipped_gap);
                return false;
            }
            if (garbage <= reclaim_last_garbage_[g]) {
                E25_INC(reclaim_skipped_unchanged);
                return false;
            }
        }
        reclaim_last_op_[g] = placements_since_;
        reclaim_last_garbage_[g] = garbage;
        return true;
    }

    // E25 V2: place using the group id and tag the locate already produced, instead
    // of asking the model and the fingerprint again. Duplicate detection is NOT
    // skipped -- the caller only reaches here after the locate said Absent.
    //
    // Compaction may run here and move slots, but it does not change the group or
    // the tag, so those stay valid; put_primary recomputes the physical position.
    bool place_with_ctx(std::string_view key, std::uint64_t value, const Located& loc) {
        if (reclaim_gated()) {
            ++placements_since_;   // E31H/E31P: the interval and the state scale
        }
        const auto s = key.substr(prefix_.size());
        if (!put_primary(loc.group, s, value, loc.tag)) {
            // E31H: the same reclaim, gated by the hysteresis when that switch is on.
            if (config_.subblock_reclaim && reclaim_allowed(loc.group)) {
                E25_INC(subblock_reclaims);
                repack_subblock(loc.group, first_candidate(loc.group, s));
                if (put_primary(loc.group, s, value, loc.tag)) {
                    E25_INC(subblock_reclaim_helped);
                    ++size_;
                    return true;
                }
            }
            // E26: compact() is reached from exactly here. Whether the retry then
            // SUCCEEDS is the direct measure of what that compaction did for the
            // operation that triggered it. A failure is recorded as "did not enable
            // this placement", NOT as "the compaction had no value".
            E25_INC(compact_entered);
            if (worth_compacting(loc.group, s.size())) compact(loc.group);
            if (put_primary(loc.group, s, value, loc.tag)) {
                E25_INC(compact_helped);
            } else {
                E25_INC(compact_no_help);
                put_overflow(loc.group, s, value, loc.tag);
            }
        }
        ++size_;
        return true;
    }

    bool insert(std::string_view key, std::uint64_t value) {
        if (!prefix_matches(key)) throw std::invalid_argument("prefix requires rebuild");

        if (config_.write_fast_locate) {
            // V1 search, V2 reuse. Without reuse the mode does not matter for
            // correctness, and holding it fixed between V1 and V2 makes the measured
            // difference "how many searches ran" and nothing else.
            Located loc;
            const Result r = find_located(key, loc);
            if (r.found) return false;
            if (config_.reuse_locate_ctx && loc.where == Where::Absent)
                return place_with_ctx(key, value, loc);
            // V1 without reuse, or a NotLocated result: keep the original flow.
            if (config_.reuse_locate_ctx && loc.where == Where::NotLocated)
                E25_INC(notlocated_fallback);
            return insert_slow(key, value);
        }

        Mode m{config_.canonical, Prefetch::Off, true, false};
        if (find(key, m).found) return false;
        return insert_slow(key, value);
    }

    // The placement half of insert, with the group id and tag computed the
    // original way. Kept separate so V1's control flow is visibly unchanged.
    bool insert_slow(std::string_view key, std::uint64_t value) {
        if (reclaim_gated()) {
            ++placements_since_;   // E31H/E31P: the interval and the state scale
        }
        const auto e30_t0 = E30_T0();
        E30_INC(insert_calls);
        struct E30Scope { std::uint64_t t0; ~E30Scope() { E30_ADD(insert_ns, t0); } } e30_scope{e30_t0};
        auto s = key.substr(prefix_.size());
        const auto g = canonical_group(s);
        auto tag = Fingerprint::of(s);

        if (!put_primary(g, s, value, tag)) {
            // E31: reclaim the rank-selected sub-block in place, then retry. Cheap
            // (6-11 ns/record) and the boundary does not change, so nothing a caller
            // holds is invalidated.
            // E31H: the same reclaim, gated by the hysteresis when that switch is on.
            if (config_.subblock_reclaim && reclaim_allowed(g)) {
                E25_INC(subblock_reclaims);
                repack_subblock(g, first_candidate(g, s));
                if (put_primary(g, s, value, tag)) {
                    E25_INC(subblock_reclaim_helped);
                    ++size_;
                    return true;
                }
            }
            // E26: compact() is reached from exactly here. Whether the retry then
            // SUCCEEDS is the direct measure of what that compaction did for the
            // operation that triggered it. A failure is recorded as "did not enable
            // this placement", NOT as "the compaction had no value".
            E25_INC(compact_entered);
            if (worth_compacting(g, s.size())) compact(g);
            if (put_primary(g, s, value, tag)) {
                E25_INC(compact_helped);
            } else {
                E25_INC(compact_no_help);
                put_overflow(g, s, value, tag);
            }
        }
        ++size_;
        return true;
    }

    // E25 V2: one locate, then either write or place. Returns true when an existing
    // record was written and false when a new one was placed -- the same convention
    // Index::upsert exposes. Duplicate detection IS the locate, so it is not skipped.
    //
    // Region level ONLY: the caller is responsible for the region-level control flow
    // that Index::insert owns (the prefix-mismatch rebuild and the region split). If
    // this were reachable from Index::upsert directly, a "one locate" that skips
    // those would be a different structure, not a faster one.
    bool upsert_one(std::string_view key, std::uint64_t value) {
        Located loc;
        const Result r = find_located(key, loc);
        if (r.found) { store_at(loc, value); return true; }
        if (loc.where == Where::Absent) { place_with_ctx(key, value, loc); return false; }
        E25_INC(notlocated_fallback);   // no context to reuse: the old path runs
        insert_slow(key, value);
        return false;
    }

    // E25 V1: one search through the query machinery, returning the position too.
    Result find_located(std::string_view key, Located& loc) const {
        const Mode m = write_mode();
        return m.fingerprint ? find_impl<true>(key, m, &loc) : find_impl<false>(key, m, &loc);
    }

    bool update(std::string_view key, std::uint64_t value) {
        if (!size_ || !prefix_matches(key)) return false;
        auto s = key.substr(prefix_.size());

        // E25 V1: locate through the tag filter / SIMD / boundary directory / overflow
        // policy, then write in place. The slow path below is the E24 behaviour and is
        // kept reachable so V0 and V1 are one binary apart.
        if (config_.write_fast_locate) {
            Located loc;
            if (!find_located(key, loc).found) return false;
            store_at(loc, value);
            return true;
        }

        const auto g = canonical_group(s);

        for (std::size_t blk = 0, nb = subblocks(g); blk < nb; ++blk) {
            E24_UP_BLOCK();
            auto* es = entries(g, blk);
            for (std::size_t i = 0; i < header(g, blk).count; ++i) {
                E24_UP_ENTRY();
                if (E25_CMP(suffix(g, blk, es[i]), s) == 0) { es[i].value = value; return true; }
            }
        }
        if (auto* o = overflow(g))
            for (auto& e : *o) {
                E24_UP_ENTRY();
                if (E25_CMP(e.suffix, s) == 0) { e.value = value; return true; }
            }
        return false;
    }

    bool erase(std::string_view key) {
        const auto e30_t0 = E30_T0();
        E30_INC(erase_calls);
        struct E30Scope { std::uint64_t t0; ~E30Scope() { E30_ADD(erase_ns, t0); } } e30_scope{e30_t0};
        if (!size_ || !prefix_matches(key)) return false;
        auto s = key.substr(prefix_.size());

        // E25 V1: same search, then the removal that the found position implies.
        if (config_.write_fast_locate) {
            Located loc;
            if (!find_located(key, loc).found) return false;
            return erase_at(loc);
        }

        const auto g = canonical_group(s);

        for (std::size_t blk = 0, nb = subblocks(g); blk < nb; ++blk) {
            E24_ERA_BLOCK();
            auto& h = header(g, blk);
            auto* es = entries(g, blk);
            for (std::size_t i = 0; i < h.count; ++i) {
                E24_ERA_ENTRY();
                if (E25_CMP(suffix(g, blk, es[i]), s) == 0) {
                    E25_ADD(erase_shifts, h.count - 1 - i);
                    E25_ADD(erase_released_primary, es[i].length);
                    for (std::size_t j = i + 1; j < h.count; ++j) {
                        es[j - 1] = es[j];
                        h.tags[j - 1] = h.tags[j];
                    }
                    --h.count;
                    --size_;
                    // Removing the first entry changes this sub-block's minimum, and
                    // emptying it removes the boundary entirely.
                    if (i == 0) rebuild_dir(g);
                    return true;
                }
            }
        }
        if (auto* o = overflow(g))
            for (auto it = o->begin(); it != o->end(); ++it) {
                E24_ERA_ENTRY();
                if (E25_CMP(it->suffix, s) == 0) {
                    E25_ADD(erase_shifts, static_cast<std::size_t>(o->end() - it) - 1);
                    E25_ADD(erase_released_overflow, it->suffix.size());
                    E25_INC(string_frees);
                    o->erase(it);
                    --size_;
                    --overflow_count_;
                    return true;
                }
            }
        return false;
    }

#ifdef E25_DIAG
    // E26 long-run diagnosis, step 2: why does a placement get refused?
    //
    // READ-ONLY. Nothing here mutates the index: every level is a question asked of a
    // snapshot, which is what the review required (trials happen on a copy or a
    // read-only simulation, never on the timed object).
    //
    //   F0  the rule as it stands -- would put_primary take this key, and if not,
    //       which of its candidate sub-blocks refused and why
    //   F1  tidy ONLY the candidate sub-block (reclaim its own garbage, keep ownership
    //       and geometry): does the key fit then? If yes, byte fragmentation in that
    //       sub-block is the direct cause; if no, F2/F3 are the places to look
    //   F2  repack the WHOLE group within its existing budget: does the budget suffice
    //       at all?
    struct TrialPlace {
        std::size_t candidates = 0;       // sub-blocks put_primary would try
        std::size_t refused_by_bytes = 0;
        std::size_t refused_by_slots = 0;
        bool f0_fits = false;
        bool f1_fits = false;             // after tidying ANY candidate put_primary tries
        // The rank-restricted version: only the FIRST candidate put_primary considers.
        // E28 reported f1_fits as if it were this one. Kept BESIDE it so the gap between
        // "some candidate" and "the rank-selected candidate" is visible instead of assumed.
        bool f1_rank_fits = false;
        std::size_t cand_subblock = 0;      // which sub-block the first candidate is
        std::size_t cand_live_records = 0;  // live records in the first candidate
        std::size_t cand_headroom = 0;      // area - live in the first candidate = free + garbage
        bool f2_budget_suffices = false;  // whole-group repack, same budget
        std::size_t cand_garbage = 0;     // garbage in the first candidate (bytes)
        std::size_t cand_free_bytes = 0;
        std::size_t group_garbage = 0;
        std::size_t group_free_bytes = 0;
        std::size_t group_free_slots = 0;
        std::size_t group_live_bytes = 0;
    };

    TrialPlace trial_place(std::uint32_t g, std::string_view s) const {
        TrialPlace t;
        const std::size_t nb = subblocks(g);
        const std::size_t area = std::size_t(bb_[g]) - start_of(g);
        const std::size_t len = s.size();

        // The group's own totals, for F2's budget test.
        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);
            const auto* es = entries(g, i);
            std::size_t live = 0;
            for (std::size_t j = 0; j < h.count; ++j) live += es[j].length;
            t.group_live_bytes += live;
            const std::size_t occupied = std::size_t(bb_[g]) - h.bottom;
            if (occupied > live) t.group_garbage += occupied - live;
            t.group_free_bytes += std::size_t(h.bottom) - start_of(g);
            t.group_free_slots += slots_of(g) - h.count;
        }
        t.f2_budget_suffices = (t.group_live_bytes + len <= area * nb) &&
                               (t.group_free_slots > 0);

        // F0: put_primary's candidate order. Replicate its two steps.
        auto consider = [&](std::size_t i) {
            const auto& h = header(g, i);
            const std::size_t freeb = std::size_t(h.bottom) - start_of(g);
            const bool slot_ok = h.count < slots_of(g);
            const bool bytes_ok = len <= freeb;
            if (t.candidates == 0) { t.cand_free_bytes = freeb; }
            ++t.candidates;
            if (slot_ok && bytes_ok) t.f0_fits = true;
            if (!slot_ok) ++t.refused_by_slots;
            else if (!bytes_ok) ++t.refused_by_bytes;

            // F1: tidy this sub-block -- reclaim its own garbage, keep its geometry.
            const auto* es = entries(g, i);
            std::size_t live = 0;
            for (std::size_t j = 0; j < h.count; ++j) live += es[j].length;
            const std::size_t occupied = std::size_t(bb_[g]) - h.bottom;
            const std::size_t garb = occupied > live ? occupied - live : 0;
            const bool first = (t.candidates == 1);
            if (first) {
                t.cand_subblock = i;
                t.cand_garbage = garb;
                t.cand_live_records = h.count;
                t.cand_headroom = area - live;      // = free bytes + garbage
            }
            const std::size_t tidied_free = area - live;   // repacked: live then free
            if (slot_ok && len <= tidied_free) {
                t.f1_fits = true;
                if (first) t.f1_rank_fits = true;   // the rank-restricted claim
            }
        };

        std::size_t prev = SIZE_MAX;
        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);
            if (h.count == 0) continue;
            const auto* es = entries(g, i);
            if (compare(suffix(g, i, es[h.count - 1]), s) < 0) { prev = i; continue; }
            if (prev != SIZE_MAX && compare(s, suffix(g, i, es[0])) < 0) consider(prev);
            consider(i);
            return t;
        }
        for (std::size_t j = prev == SIZE_MAX ? 0 : prev; j < nb; ++j) consider(j);
        return t;
    }

    // E26 long-run diagnosis: which logical group does a key map to, and which
    // sub-block does that group's key order select? Diagnostic only; the physical
    // group id is the thing the capacity plan was built for, so a replacement that
    // changes it is a different placement question from one that does not.
    std::size_t diag_prefix_size() const { return prefix_.size(); }
    // E31 item 3: the live overflow containers' record counts, so the size distribution
    // at a checkpoint is a measurement rather than an inference from the record total.
    // Empty containers are skipped: a group that never overflowed would otherwise pull
    // the mean toward zero and make a few large containers look harmless. Read-only, and
    // called with the clock stopped.
    void diag_overflow_sizes(std::vector<std::size_t>& out) const {
        for (std::uint32_t g = 0; g < groups_; ++g)
            if (const auto* o = overflow(g))
                if (!o->empty()) out.push_back(o->size());
    }
    // The element size the movement byte counters are counted in, so a reader can turn
    // "shifted bytes" back into "shifted records" without guessing. sizeof() on the
    // private record type can only be named where the type is visible, which is here.
    static constexpr std::size_t diag_overflow_record_bytes() { return sizeof(OverflowRecord); }
    std::uint32_t diag_group(std::string_view key) const {
        if (!prefix_matches(key)) return UINT32_MAX;   // no valid group
        return canonical_group(key.substr(prefix_.size()));
    }
#endif

#ifdef E30_TIME
    // E30 probe: the per-record cost of an in-place sub-block tightening, measured on a
    // scratch copy so the index is never touched. mutable because the probe is const.
    mutable std::vector<std::byte> e30_scratch_;
    mutable std::vector<std::size_t> e30_order_;

    void e30_repack_probe(std::uint32_t g, std::size_t blk, std::uint64_t& copy_ns,
                          std::uint64_t& pass_ns, std::size_t& records,
                          std::uint64_t& moved_bytes, bool& pass_ok) const {
        const std::size_t bytes = bb_[g];
        if (e30_scratch_.size() < bytes) e30_scratch_.resize(bytes);
        if (e30_order_.size() < slots_of(g)) e30_order_.resize(slots_of(g));

        const auto tc0 = E30_T0();
        std::memcpy(e30_scratch_.data(), base_of(g, blk), bytes);
        copy_ns = E30_T0() - tc0;

        std::byte* buf = e30_scratch_.data();
        auto* h = reinterpret_cast<Header*>(buf);
        auto* es = reinterpret_cast<Entry*>(buf + sizeof(Header));
        const std::size_t n = h->count;
        records = n;
        moved_bytes = 0;
        pass_ok = true;
        if (n == 0) { pass_ns = 0; return; }

        // Snapshot the pre-pass view so the pass can be checked, not argued.
        std::vector<std::string> before;
        before.reserve(n);
        for (std::size_t j = 0; j < n; ++j)
            before.emplace_back(reinterpret_cast<const char*>(buf + es[j].offset), es[j].length);

        const std::size_t start = sizeof(Header) + slots_of(g) * sizeof(Entry);
        const auto tp0 = E30_T0();

        // Slot order, sorted by offset DESCENDING (insertion sort; n <= 48).
        for (std::size_t j = 0; j < n; ++j) e30_order_[j] = j;
        for (std::size_t a = 1; a < n; ++a) {
            const std::size_t key = e30_order_[a];
            const auto koff = es[key].offset;
            std::size_t b = a;
            while (b > 0 && es[e30_order_[b - 1]].offset < koff) {
                e30_order_[b] = e30_order_[b - 1];
                --b;
            }
            e30_order_[b] = key;
        }

        std::size_t top = bytes;
        for (std::size_t a = 0; a < n; ++a) {
            Entry& e = es[e30_order_[a]];
            top -= e.length;
            std::memmove(buf + top, buf + e.offset, e.length);
            moved_bytes += e.length;
            e.offset = static_cast<std::uint32_t>(top);
        }
        h->bottom = static_cast<std::uint16_t>(top);
        pass_ns = E30_T0() - tp0;

        if (top < start) { pass_ok = false; return; }
        for (std::size_t j = 0; j < n; ++j) {
            const std::string_view now(reinterpret_cast<const char*>(buf + es[j].offset), es[j].length);
            if (now != before[j]) { pass_ok = false; break; }
        }
    }
#endif

#ifdef E24_DIAG
    // E24: live per-group geometry, walked from the REAL structure -- not the
    // static GroupStats model, which describes an input rather than the current
    // layout. Appends one report per logical group.
    void e24_report(std::vector<e24::GroupReport>& out) const {
        for (std::uint32_t g = 0; g < groups_; ++g) {
            e24::GroupReport rep;
            rep.group = g;
            const std::size_t nb = subblocks(g);
            rep.subs.reserve(nb);
            for (std::size_t blk = 0; blk < nb; ++blk) {
                const auto& h = header(g, blk);
                e24::SubBlock sb;
                sb.slots = slots_of(g);
                sb.used = h.count;
                sb.suffix_cap = std::size_t(bb_[g]) - start_of(g);
                const auto* es = entries(g, blk);
                for (std::size_t i = 0; i < h.count; ++i) sb.suffix_live += es[i].length;
                sb.suffix_free = std::size_t(h.bottom) - start_of(g);
                sb.occupied = sb.suffix_cap - sb.suffix_free;
                sb.garbage = sb.occupied - sb.suffix_live;   // deleted bytes not reclaimed
                rep.subs.push_back(sb);
            }

            if (const auto* o = overflow(g)) {
                rep.ov_container_cap = o->capacity() * sizeof(OverflowRecord);
                rep.ov_rec_size = sizeof(OverflowRecord);
                Result tmp;
                for (const auto& e : *o) {
                    ++rep.ov_records;
                    rep.ov_live_bytes += e.suffix.size();
                    rep.ov_suffix_cap += e.suffix.capacity() + 1;
                    const std::size_t L = e.suffix.size();

                    // M1: with more than one sub-block, key order selects exactly ONE
                    // candidate. With a single sub-block the choice is vacuous, and
                    // within-block sorted insert handles the position.
                    std::size_t cand = 0;
                    if (nb == 0) { ++rep.ov_blocked_order; continue; }
                    if (nb > 1) {
                        cand = has_dir(g) ? locate_subblock(g, e.suffix, tmp) : 0;
                        if (cand == NO_SUBBLOCK) { ++rep.ov_blocked_order; continue; }
                    }

                    const auto& h = header(g, cand);
                    const bool slot_ok = h.count < slots_of(g);
                    const bool bytes_ok = L <= std::size_t(h.bottom) - start_of(g);
                    if (slot_ok && bytes_ok) {
                        ++rep.ov_fits_ordered;
                        rep.ov_fits_ordered_bytes += L;
                    } else if (!slot_ok) {
                        ++rep.ov_blocked_slots;
                    } else {
                        ++rep.ov_blocked_bytes;
                    }
                    // What the existing coarse predicate would claim instead.
                    if (group_has_room(g, L)) ++rep.ov_fits_if_any_subblock;
                }
            }
            out.push_back(std::move(rep));
        }
    }
#endif

    // Ordered merge of primary entries and overflow; the group count is monotone
    // in the key order, so scanning groups in ascending order yields sorted output.
    void scan(std::string_view start, std::size_t limit, std::vector<Pair>& out) const {
        if (!size_ || out.size() >= limit) return;

        bool matches = prefix_matches(start);
        if (!matches && compare(start, prefix_) > 0) return;
        const auto s = matches ? start.substr(prefix_.size()) : std::string_view{};
        const auto first = matches ? canonical_group(s) : 0;

        for (std::uint32_t g = first; g < groups_ && out.size() < limit; ++g) {
            E25_INC(scan_groups);
            const auto* o = overflow(g);
            const std::size_t nb = subblocks(g);
            std::size_t blk = 0, a = 0, b = 0;

            if (g == first && matches) {
                // Advance the primary cursor past every entry < s. Sub-blocks are
                // rank-ordered, so this skips whole sub-blocks at a time.
                while (blk < nb) {
                    while (a < header(g, blk).count &&
                           compare(suffix(g, blk, entries(g, blk)[a]), s) < 0)
                        ++a;
                    if (a < header(g, blk).count) break;
                    ++blk;
                    a = 0;
                }
                if (o) while (b < o->size() && compare((*o)[b].suffix, s) < 0) ++b;
            }

            while (out.size() < limit) {
                // Skip empty and exhausted sub-blocks before looking at the head.
                // This must happen here and not only when the last entry is taken:
                // an emptied sub-block in the middle would otherwise end the whole
                // group's scan and silently drop every later sub-block's records.
                while (blk < nb && a >= header(g, blk).count) {
                    E25_INC(scan_blocks_skipped);
                    ++blk;
                    a = 0;
                }

                const bool have_a = blk < nb;
                const bool have_b = o && b < o->size();
                if (!have_a && !have_b) {
                    if (blk == 0) E25_INC(scan_groups_empty);   // nothing here at all
                    break;
                }

                bool take_a = have_a;
                if (have_a && have_b) {
                    E25_INC(scan_merge_cmps);
                    take_a = compare(suffix(g, blk, entries(g, blk)[a]), (*o)[b].suffix) < 0;
                }
                if (take_a) {
                    const auto suf = suffix(g, blk, entries(g, blk)[a]);
                    // NOTE: without the flag this is prefix_ + std::string(suffix): a
                    // temporary string plus a concatenation. Counted as the allocations
                    // it is -- 2, not 1.
                    E25_ADD(scan_result_allocs, config_.scan_reserve_result ? 1 : 2);
                    if (config_.scan_reserve_result) {
                        std::string full;
                        full.reserve(prefix_.size() + suf.size());
                        full.append(prefix_);
                        full.append(suf);
                        out.emplace_back(std::move(full), entries(g, blk)[a].value);
                    } else {
                        out.emplace_back(prefix_ + std::string(suf),
                                         entries(g, blk)[a].value);
                    }
                    ++a;
                } else {
                    E25_ADD(scan_result_allocs, 1);
                    out.emplace_back(prefix_ + (*o)[b].suffix, (*o)[b].value);
                    ++b;
                }
            }
        }
    }

    std::vector<Pair> all() const {
        const auto t0 = E14_T0();
        std::vector<Pair> out;
        out.reserve(size_);
        scan({}, size_, out);
        E14_ADD(region_all, t0);
        return out;
    }
};

// ---------------------------------------------------------------------------
// Index: outer router over ordered region fences.
// ---------------------------------------------------------------------------

// Test-only accessor, declared here and defined after the class. It exists so the
// round's gate can build counter-examples with a mismatched or stale context;
// nothing in the library or the benchmarks should go through it.
struct IndexTestAccess;

class Index {
    Config config_;
    struct Node {
        std::string fence;
        std::unique_ptr<Region> region;
    };
    std::vector<Node> nodes_;
    std::size_t size_ = 0;

    // E6-1: bumped on every fence-changing event so auxiliary routing metadata
    // (built outside the index) can detect staleness and rebuild.
    std::uint64_t structure_version_ = 0;

public:
    std::size_t rebuilds = 0, splits = 0;

    explicit Index(Config c) : config_(c) {}

    const std::string& fence(std::size_t i) const { return nodes_[i].fence; }
    std::uint64_t structure_version() const { return structure_version_; }

    void bulk_load(const std::vector<Pair>& pairs) {
        const auto e14_t0 = E14_T0();
        nodes_.clear();
        size_ = pairs.size();
        const auto chunk = config_.region_keys ? config_.region_keys : std::max<std::size_t>(1, pairs.size());
        for (std::size_t p = 0; p < pairs.size(); p += chunk) {
            std::vector<Pair> part(pairs.begin() + p, pairs.begin() + std::min(p + chunk, pairs.size()));
            nodes_.push_back({part.front().first, std::make_unique<Region>(part, config_)});
        }
        if (nodes_.empty())
            nodes_.push_back({{}, std::make_unique<Region>(std::vector<Pair>{}, config_)});
        ++structure_version_;
        E14_ADD(bulk_load, e14_t0);
    }

    // -----------------------------------------------------------------------
    // E15: build every region directly under a supplied capacity plan.
    //
    // This replaces the pair "bulk_load under the DEFAULT capacity, then one
    // rebuild_with_caps per region". Both halves construct a full Region per
    // region, but the first half's only consumer is Region::all(), which returns
    // a contiguous slice of the sorted input -- exactly the slice this method
    // passes in directly. E14 measured that discarded half at 55.6%-66.3% of the
    // build (E14_TABLES.md §3), so it is pure cost.
    //
    // The chunking, the fences and the region count are bulk_load's, and the
    // constructor call per region is rebuild_with_caps's, so the structure this
    // produces is the same one the two-pass pipeline produces. That is not
    // asserted here -- bench_e15 compares layout digests before it reports any
    // timing, and the digest covers the partition, the prefix, all three capacity
    // tables, the derived sub-block structure, the boundary directory and every
    // stored (key, value).
    //
    // A plan that does not cover every region is rejected rather than silently
    // padded, and a per-region shape that does not match that region's group
    // count is rejected by the Region constructor itself.
    //
    // Strong exception guarantee: the plan is validated and every Region is built
    // into a LOCAL vector, which is swapped in only once all of them succeeded. A
    // rejected plan therefore leaves the index in its previous state rather than
    // half-built. (Without this, a mid-loop throw left `nodes_` holding some of
    // the new regions while `size_` already reported the full key count -- an
    // internally inconsistent object for any caller that caught the exception.
    // check_e15_plan's case A1 asserts the guarantee directly.)
    // -----------------------------------------------------------------------
    void bulk_load_with_plan(const std::vector<Pair>& pairs,
                             const std::vector<std::vector<std::uint16_t>>& sg,
                             const std::vector<std::vector<std::uint32_t>>& bb,
                             const std::vector<std::vector<std::uint16_t>>& nb_cap = {}) {
        bulk_load_with_context(pairs, sg, bb, nb_cap, nullptr);
    }

    // INTERNAL. Takes a BuildContext, which must not be held across calls or
    // replayed against other data -- see bulk_load_reusing_ids. Exposed only so the
    // round's gate can exercise the misuse cases; callers use the one-call form.
 private:
    friend struct IndexTestAccess;

    // PRIVATE. Takes a BuildContext, which must not be held across calls or replayed
    // against other data -- see bulk_load_reusing_ids. Reachable only through
    // IndexTestAccess. Ordinary callers use the one-call form and cannot obtain a
    // context at all, which is the point: the guarantee is the lifetime.
    void bulk_load_with_context(const std::vector<Pair>& pairs,
                                const std::vector<std::vector<std::uint16_t>>& sg,
                                const std::vector<std::vector<std::uint32_t>>& bb,
                                const std::vector<std::vector<std::uint16_t>>& nb_cap,
                                const BuildContext* ctx) {
        const auto e15_t0 = E14_T0();
        E14_INC(bulk_planned);
        if (!nb_cap.empty() && nb_cap.size() != sg.size())
            throw std::invalid_argument("E15: sub-block cap table size");
        const auto chunk = config_.region_keys ? config_.region_keys : std::max<std::size_t>(1, pairs.size());

        // E21: a context is only usable on the very key vector it was built from.
        // Address and size are the cheap half of that; the per-region key digest is
        // the half that survives an in-place edit.
        const bool source_ok = ctx != nullptr && ctx->source == pairs.data() &&
                               ctx->source_size == pairs.size();

        std::vector<Node> fresh;
        std::size_t r = 0;
        for (std::size_t p = 0; p < pairs.size(); p += chunk, ++r) {
            if (r >= sg.size() || r >= bb.size())
                throw std::invalid_argument("E15: capacity plan does not cover every region");
            const Pair* f = pairs.data() + p;
            const Pair* l = pairs.data() + std::min(p + chunk, pairs.size());
            std::vector<Pair> part(pairs.begin() + p, pairs.begin() + std::min(p + chunk, pairs.size()));
            std::vector<std::uint16_t> cap;
            if (!nb_cap.empty()) cap = nb_cap[r];
            // E21: the context is accepted only for the key vector it was built
            // from. `source_ok` is checked once per call; the per-region key digest
            // then confirms the sequence has not been edited or reordered.
            const RegionDemand* preset = nullptr;
            if (source_ok && r < ctx->regions.size()) {
                const RegionDemand& rd = ctx->regions[r];
                if (rd.group_ids.size() == std::size_t(l - f) &&
                    rd.key_digest == Region::key_sequence_digest(f, l))
                    preset = &rd;
            }
            fresh.push_back({part.front().first,
                             std::make_unique<Region>(part, config_, sg[r], bb[r], std::move(cap),
                                                      preset)});
        }
        if (fresh.empty())
            fresh.push_back({{}, std::make_unique<Region>(std::vector<Pair>{}, config_)});

        nodes_ = std::move(fresh);
        size_ = pairs.size();
        ++structure_version_;
        E14_ADD(bulk_planned, e15_t0);
    }

 public:

    // E22: the capacity plan a construction runs under, as returned by the
    // caller's callback in bulk_load_reusing_ids.
    struct CapacityPlan {
        std::vector<std::vector<std::uint16_t>> sg;
        std::vector<std::vector<std::uint32_t>> bb;
        std::vector<std::vector<std::uint16_t>> caps;
    };

    // -----------------------------------------------------------------------
    // E22 (R2 as an option): statistics, planning and construction in ONE call,
    // with the group ids reused between the first and the third.
    //
    // This is the ONLY documented way to use the reuse. The region context is an
    // ordinary local here and never escapes, so it cannot be stored and later
    // replayed against a different key array. That is the guarantee: a controlled
    // lifetime, not a hash. The per-region checks (source, key digest, prefix,
    // group count, model digest) remain as defence in depth and will refuse an
    // obvious mismatch, but they are not what makes this correct.
    //
    // `stats_cfg` is the config the statistics replay runs under (single-block,
    // which is what the pipeline probes with); this index is built under its own
    // config_. `plan_for` is called once with the per-region statistics and returns
    // the capacity plan to build under -- a callback, so this header does not have
    // to depend on the allocator.
    // -----------------------------------------------------------------------
    template <class PlanFor>
    void bulk_load_reusing_ids(const std::vector<Pair>& pairs, const Config& stats_cfg,
                               PlanFor&& plan_for) {
        // The statistics replay covers the single-block layout, which is what the
        // pipeline probes with; this index is built under its own config_. Two
        // configs, one call -- and `ctx` is an ordinary local either way.
        Index stats(stats_cfg);
        const BuildContext ctx = stats.demand_profile_ctx(pairs);
        const CapacityPlan plan = plan_for(ctx.regions);
        bulk_load_with_context(pairs, plan.sg, plan.bb, plan.caps, &ctx);
    }

    // -----------------------------------------------------------------------
    // E16: the probe's OUTPUT -- per-region, per-group statistics -- without
    // building the probe.
    //
    // Splits `pairs` exactly as bulk_load does, and hands each chunk to
    // Region::demand_profile as a POINTER RANGE, so unlike `probe.bulk_load(keys)`
    // this materialises no per-region vector<Pair> and allocates no blocks.
    //
    // The pipeline's probe is built with the same Config this Index carries, so
    // `probe.region(i).group_stats()` and `index.demand_profile(keys)[i]` are
    // meant to be the same vector. check_e16_probe asserts that field by field.
    // -----------------------------------------------------------------------
    BuildContext demand_profile_ctx(const std::vector<Pair>& pairs) const {
        BuildContext bc;
        bc.source = pairs.data();
        bc.source_size = pairs.size();
        const auto chunk = config_.region_keys ? config_.region_keys : std::max<std::size_t>(1, pairs.size());
        for (std::size_t p = 0; p < pairs.size(); p += chunk) {
            const Pair* f = pairs.data() + p;
            const Pair* l = pairs.data() + std::min(p + chunk, pairs.size());
            bc.regions.push_back(Region::demand_profile_ctx(f, l, config_));
        }
        if (bc.regions.empty())
            bc.regions.push_back(Region::demand_profile_ctx(nullptr, nullptr, config_));
        return bc;
    }

    std::vector<std::vector<GroupStats>> demand_profile(const std::vector<Pair>& pairs) const {
        std::vector<std::vector<GroupStats>> out;
        const auto chunk = config_.region_keys ? config_.region_keys : std::max<std::size_t>(1, pairs.size());
        for (std::size_t p = 0; p < pairs.size(); p += chunk) {
            const Pair* f = pairs.data() + p;
            const Pair* l = pairs.data() + std::min(p + chunk, pairs.size());
            out.push_back(Region::demand_profile(f, l, config_));
        }
        if (out.empty()) out.push_back(Region::demand_profile(nullptr, nullptr, config_));
        return out;
    }

    std::size_t locate(std::string_view key) const {
        E25_INC(locate_calls);
        if (nodes_.empty()) throw std::logic_error("bulk_load must initialize index");
        auto it = std::upper_bound(nodes_.begin(), nodes_.end(), key,
            [](std::string_view k, const Node& n) { return compare(k, n.fence) < 0; });
        return it == nodes_.begin() ? 0 : static_cast<std::size_t>(it - nodes_.begin() - 1);
    }

    const Region& region(std::size_t i) const { return *nodes_[i].region; }
    Result find(std::string_view key, Mode m) const { return region(locate(key)).find(key, m); }
    Result find_known(std::size_t r, std::string_view key, Mode m) const { return region(r).find(key, m); }
    std::size_t size() const { return size_; }
    std::size_t region_count() const { return nodes_.size(); }

    // E8-9: memory decomposition across regions plus index-level metadata.
    struct MemBreakdown {
        std::size_t blocks = 0, model = 0, prefix = 0, overflow = 0, tables = 0,
                    directory = 0, regions_self = 0, fences = 0, self = 0;
    };

    MemBreakdown memory_breakdown() const {
        MemBreakdown m;
        for (const auto& x : nodes_) {
            const auto b = x.region->memory_breakdown();
            m.blocks += b.blocks;
            m.model += b.model;
            m.prefix += b.prefix;
            m.overflow += b.overflow;
            m.tables += b.tables;
            m.directory += b.directory;
            m.regions_self += b.self;
            m.fences += x.fence.capacity() + 1;
        }
        m.self = sizeof(*this) + nodes_.capacity() * sizeof(Node);
        return m;
    }

#ifdef E25_DIAG
    // E26 long-run diagnosis: the logical group a key maps to, and the region it
    // routes to. Wraps the Region accessor; diagnostic only.
    std::pair<std::size_t, std::uint32_t> diag_where(std::string_view key) const {
        const std::size_t r = locate(key);
        return {r, nodes_[r].region->diag_group(key)};
    }
    // Read-only F0/F1/F2 trial for a key, on the CURRENT state. Does not mutate.
#ifdef E30_TIME
    // E30: probe the rank-selected candidate sub-block of `key`, on a scratch copy.
    void e30_probe(std::string_view key, std::uint64_t& copy_ns, std::uint64_t& pass_ns,
                   std::size_t& records, std::uint64_t& moved_bytes, bool& ok) const {
        const std::size_t r = locate(key);
        const Region& reg = *nodes_[r].region;
        const std::uint32_t g = reg.diag_group(key);
        if (g == UINT32_MAX) { copy_ns = pass_ns = 0; records = 0; moved_bytes = 0; ok = true; return; }
        const auto tp = reg.trial_place(g, key.substr(reg.diag_prefix_size()));
        reg.e30_repack_probe(g, tp.cand_subblock, copy_ns, pass_ns, records, moved_bytes, ok);
    }
#endif

    // E31 item 3: every live overflow container's record count, over all regions, plus
    // the element size the movement counters are expressed in. Diagnostic only.
    void diag_overflow_sizes(std::vector<std::size_t>& out) const {
        for (const auto& x : nodes_) x.region->diag_overflow_sizes(out);
    }
    std::size_t diag_overflow_record_bytes() const { return Region::diag_overflow_record_bytes(); }

    Region::TrialPlace diag_trial(std::string_view key) const {
        const std::size_t r = locate(key);
        const Region& reg = *nodes_[r].region;
        const std::uint32_t g = reg.diag_group(key);
        if (g == UINT32_MAX) return {};
        return reg.trial_place(g, key.substr(reg.diag_prefix_size()));
    }
#endif

#ifdef E24_DIAG
    // E24: every group of every region, for the space decomposition and the
    // reclaim analysis. Diagnostic only.
    std::vector<e24::GroupReport> e24_report() const {
        std::vector<e24::GroupReport> out;
        for (const auto& x : nodes_) x.region->e24_report(out);
        return out;
    }
#endif

    std::size_t memory() const {
        std::size_t n = sizeof(*this) + nodes_.capacity() * sizeof(Node);
        for (const auto& x : nodes_) n += x.fence.capacity() + 1 + x.region->memory();
        return n;
    }

    std::size_t overflow_count() const {
        std::size_t n = 0;
        for (const auto& x : nodes_) n += x.region->overflow_count();
        return n;
    }

    std::size_t compaction_count() const {
        std::size_t n = 0;
        for (const auto& x : nodes_) n += x.region->compaction_count();
        return n;
    }

    // E8: replace a region with an explicit per-group capacity table (no split;
    // caller guarantees data fits the region size). Caps only change capacity,
    // never semantics.
    void rebuild_with_caps(std::size_t i, std::vector<Pair> data,
                           std::vector<std::uint16_t> sg, std::vector<std::uint32_t> bb,
                           std::vector<std::uint16_t> nb_cap = {}) {
        const auto e14_t0 = E14_T0();
        E14_INC(rebuild_calls);
        nodes_[i].region = std::make_unique<Region>(data, config_, std::move(sg), std::move(bb),
                                                    std::move(nb_cap));
        ++rebuilds;
        ++structure_version_;
        E14_ADD(rebuild, e14_t0);
    }

    void rebuild(std::size_t i, std::vector<Pair> data) {
        const auto e14_t0 = E14_T0();
        E14_INC(rebuild_calls);
        E25_INC(region_rebuilds);
        if (config_.region_keys && data.size() > 2 * config_.region_keys) {
            const auto mid = data.size() / 2;
            std::vector<Pair> left(data.begin(), data.begin() + mid), right(data.begin() + mid, data.end());
            auto a = std::make_unique<Region>(left, config_);
            auto b = std::make_unique<Region>(right, config_);
            const auto fence = right.front().first;
            nodes_[i].region = std::move(a);
            nodes_.insert(nodes_.begin() + i + 1, Node{fence, std::move(b)});
            ++splits;
        } else {
            auto fresh = std::make_unique<Region>(data, config_);
            nodes_[i].region = std::move(fresh);
        }
        ++rebuilds;
        ++structure_version_;
        E14_ADD(rebuild, e14_t0);
    }

    bool insert(std::string_view key, std::uint64_t value) {
        const auto i = locate(key);
        auto& r = *nodes_[i].region;
        if (!r.accepts_prefix(key)) {
            E25_INC(prefix_rebuilds);
            auto data = r.all();
            auto it = std::lower_bound(data.begin(), data.end(), key,
                [](const Pair& a, std::string_view b) { return compare(a.first, b) < 0; });
            if (it != data.end() && compare(it->first, key) == 0) return false;
            data.insert(it, {std::string(key), value});
            rebuild(i, std::move(data));
            ++size_;
            return true;
        }
        if (!r.insert(key, value)) return false;
        ++size_;
        if (config_.region_keys && r.size() > 2 * config_.region_keys) {
            E25_INC(threshold_splits);
            rebuild(i, r.all());
        }
        return true;
    }

    bool update(std::string_view key, std::uint64_t v) { return nodes_[locate(key)].region->update(key, v); }

    bool erase(std::string_view key) {
        bool ok = nodes_[locate(key)].region->erase(key);
        if (ok) --size_;
        return ok;
    }

    bool upsert(std::string_view key, std::uint64_t value) {
        // E25 V2: one locate decides write-or-place. V0/V1 keep the two-step form.
        if (config_.reuse_locate_ctx) {
            const auto i = locate(key);
            auto& r = *nodes_[i].region;
            // The region cannot take this key at all: no context to reuse, and the
            // existing path carries the rebuild that makes it acceptable.
            if (!r.accepts_prefix(key)) {
                if (update(key, value)) return false;
                return insert(key, value);
            }
            if (r.upsert_one(key, value)) return false;   // an existing record was written
            ++size_;
            // The region-level split Index::insert performs. Omitting it here would
            // make V2 a DIFFERENT structure rather than a faster writer.
            if (config_.region_keys && r.size() > 2 * config_.region_keys) rebuild(i, r.all());
            return true;
        }
        if (update(key, value)) return false;
        return insert(key, value);
    }

    void rebuild_all() {
        for (std::size_t i = 0; i < nodes_.size(); ++i) rebuild(i, nodes_[i].region->all());
    }

    std::vector<Pair> scan(std::string_view start, std::size_t count) const {
        std::vector<Pair> out;
        out.reserve(std::min(count, size_));
        // E27 S2 (checklist 7.1): locate(start) was re-evaluated on every iteration of
        // this loop and once more in the body -- across R regions that is R+1 outer
        // searches for one answer. The switch caches the first evaluation and uses it
        // for both the loop bound and the "is this the starting region" test, so the
        // scan pays one. scan_start_locates is incremented on EVERY evaluation in BOTH
        // arms, so the claim is measured: S0 counts R+1 per scan, S2 exactly 1.
        if (config_.scan_cache_start) {
            const std::size_t first = locate(start);
            E25_INC(scan_start_locates);
            for (std::size_t i = first; i < nodes_.size() && out.size() < count; ++i) {
                E25_INC(scan_regions);
                nodes_[i].region->scan(i == first ? start : std::string_view{}, count, out);
            }
            return out;
        }
        // Pre-E27 path. The two locate(start) expressions are exactly as they were;
        // only the no-op counter statement is new.
        std::size_t i = locate(start);
        E25_INC(scan_start_locates);
        for (; i < nodes_.size() && out.size() < count; ++i) {
            E25_INC(scan_regions);
            E25_INC(scan_start_locates);
            nodes_[i].region->scan(i == locate(start) ? start : std::string_view{}, count, out);
        }
        return out;
    }
};

// Test-only. Do not call from library code or from a benchmark: a context is
// only sound within the single call that produced it.
struct IndexTestAccess {
    static void build_with_context(Index& idx, const std::vector<Pair>& pairs,
                                   const std::vector<std::vector<std::uint16_t>>& sg,
                                   const std::vector<std::vector<std::uint32_t>>& bb,
                                   const std::vector<std::vector<std::uint16_t>>& caps,
                                   const BuildContext* ctx) {
        idx.bulk_load_with_context(pairs, sg, bb, caps, ctx);
    }
};


}  // namespace validation

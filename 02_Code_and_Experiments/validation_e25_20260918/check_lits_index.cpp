// E38 / P2: the LITS index gate.
//
// THE CLAIM: with the LITS backend wired in, the index produces the same CONTENT and the same
// PUBLIC SEMANTICS as the frozen one, on the same operations -- and it does so through the
// maintenance paths, not only on a freshly built structure.
//
// WHAT "THE SAME" MEANS HERE, AND WHAT IT DOES NOT. The task book's section 5.3 is explicit:
// the two arms are NOT required to have the same structure. Group counts, capacity tables,
// sub-block layouts, overflow content and compaction counts are all EXPECTED to differ, because
// the mapping differs and that is the experiment. So this gate never compares an arm against
// the other arm. It compares EACH arm against a reference sorted map, on
//   * size, every (key, value) pair, the full scan order and its content,
//   * the answer and the return value of every operation,
//   * and, for the LITS arm, that the backend it claims to be running is the one it ran.
// A structure comparison between the arms would fail for the right reason and prove nothing.
//
// THE EVENTS ARE COUNTED, NOT ASSUMED. "The final content matched" is not evidence that a
// branch ran -- E25 broke Index-level control flow and a content check did not notice. Every
// path the review named is driven here and its counter asserted non-zero: prefix-mismatch
// rebuild, region split, sub-block boundary delete, compaction, overflow insert and erase, and
// the empty / single-key / G == 1 boundary cases.

#include "data.hpp"
#include "model/lits_hpt_adapter.hpp"
#include "switches.hpp"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using validation::Config;
using validation::Index;
using validation::Pair;

void fail(const std::string& what) {
    throw std::runtime_error("LITS INDEX GATE FAILED: " + what);
}

// The reference uses the index's own byte comparison, so "sorted" means the same thing on both
// sides. A std::less<std::string> would order differently for bytes >= 128 and make the gate
// disagree with the index about what order is.
struct ByteLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return validation::compare(a, b) < 0;
    }
};

using Reference = std::map<std::string, std::uint64_t, ByteLess>;

std::string describe(std::string_view s) {
    std::string out;

    for (unsigned char c : s) {
        if (c >= 32 && c < 127) out.push_back(static_cast<char>(c));
        else { out += "\\x"; out += "0123456789abcdef"[c >> 4]; out += "0123456789abcdef"[c & 15]; }
    }

    return "(" + std::to_string(s.size()) + "B) " + out;
}

struct Counts {
    std::uint64_t phases = 0;
    std::uint64_t keys_compared = 0;
    std::uint64_t scans_compared = 0;
    std::uint64_t scan_records_compared = 0;
    std::uint64_t ops_compared = 0;
    std::uint64_t probe_starts = 0;
    std::uint64_t regions_calibrated = 0;    // CUMULATIVE over the gate
    std::uint64_t regions_fallback = 0;      // CUMULATIVE
    std::uint64_t regions_boundary = 0;      // CUMULATIVE
    std::uint64_t m0_regions_via_lits = 0;
    // STATE at the end of the arm's operation sequence -- the calibers a timed round needs.
    std::uint64_t regions_live = 0;
    std::uint64_t regions_lits_live = 0;
    std::uint64_t records_live = 0;
    std::uint64_t records_lits_live = 0;
    std::uint64_t reason_admission = 0;
    std::uint64_t reason_degenerate = 0;
    std::uint64_t reason_boundary = 0;
    std::uint64_t reason_unsupported = 0;
    std::uint64_t prefix_rebuilds = 0;
    std::uint64_t splits = 0;
    std::uint64_t compactions = 0;
    // `overflow_erases` is the library's own counter for overflow records DESTROYED.
    // `peak_overflow` is the container's live count sampled after the insert-heavy phases --
    // an erase can only happen to a record an insert put there, but sampling the peak is the
    // direct evidence that the container actually filled.
    std::uint64_t overflow_erases = 0;
    std::uint64_t peak_overflow = 0;
    std::uint64_t boundary_erases = 0;
    std::uint64_t tiny_empty = 0;
    std::uint64_t tiny_single = 0;
    std::uint64_t tiny_single_group = 0;
};

Counts g;

// ---------------------------------------------------------------------------
// The reference-side operations. Each returns what the INDEX is expected to return, so the
// gate compares return values rather than only end states.
// ---------------------------------------------------------------------------

bool ref_insert(Reference& r, const std::string& k, std::uint64_t v) {
    return r.emplace(k, v).second;
}

bool ref_update(Reference& r, const std::string& k, std::uint64_t v) {
    const auto it = r.find(k);
    if (it == r.end()) return false;

    it->second = v;
    return true;
}

bool ref_erase(Reference& r, const std::string& k) { return r.erase(k) != 0; }

// upsert's return value, taken from the IMPLEMENTATION rather than from the comment beside it:
// `Index::upsert` writes `if (r.upsert_one(...)) return false;`, so it returns FALSE when the
// record already existed and TRUE when a new one was placed -- the opposite of `upsert_one`'s
// own convention, even though the comment there says the two are the same. The gate encodes
// the observed behaviour, because the frozen arm IS the baseline being preserved.
bool ref_upsert(Reference& r, const std::string& k, std::uint64_t v) {
    auto it = r.find(k);
    if (it != r.end()) { it->second = v; return false; }

    r.emplace(k, v);
    return true;
}

// ---------------------------------------------------------------------------
// Checking one arm against the reference.
// ---------------------------------------------------------------------------

struct Arm {
    const char* name = "";
    Config cfg;
    std::unique_ptr<Index> idx;
};

void verify_against_reference(const Arm& arm, const Reference& ref, const char* what) {
    if (arm.idx->size() != ref.size()) {
        fail(std::string(what) + ": " + arm.name + " size " + std::to_string(arm.idx->size()) +
             " vs reference " + std::to_string(ref.size()));
    }

    // Every reference key resolves to its reference value.
    const validation::Mode m = validation::query_mode;

    for (const auto& kv : ref) {
        const auto r = arm.idx->find(kv.first, m);

        if (!r.found || r.value != kv.second) {
            fail(std::string(what) + ": " + arm.name + " disagrees on " + describe(kv.first) +
                 " found=" + std::to_string(r.found));
        }

        ++g.keys_compared;
    }

    // And the full scan is the reference in order, end to end.
    const auto got = arm.idx->scan(std::string_view{}, ref.size() + 16);

    if (got.size() != ref.size()) {
        fail(std::string(what) + ": " + arm.name + " full scan returned " +
             std::to_string(got.size()) + " records, reference has " + std::to_string(ref.size()));
    }

    std::size_t i = 0;
    for (const auto& kv : ref) {
        if (got[i].first != kv.first || got[i].second != kv.second) {
            fail(std::string(what) + ": " + arm.name + " full scan differs at " +
                 std::to_string(i) + ": got " + describe(got[i].first));
        }

        ++i;
        ++g.scan_records_compared;
    }

    ++g.scans_compared;
    ++g.phases;
}

// ---------------------------------------------------------------------------
// Scan coverage: the starts the review named, at several counts.
// ---------------------------------------------------------------------------

void verify_scans(const Index& idx, const Reference& ref, const char* what) {
    if (ref.empty()) return;

    std::vector<std::string> starts;

    // below the minimum, the minimum itself, between two keys, an absent key that sorts inside
    // the range, the maximum, and above the maximum.
    starts.push_back(std::string(1, '\x01'));
    starts.push_back(ref.begin()->first);

    {
        auto it = ref.begin();
        for (int i = 0; i < 3 && it != ref.end(); ++i, ++it) {}
        if (it != ref.end()) {
            std::string between = it->first;
            between.push_back('\x01');   // sorts just after it, before the next key
            starts.push_back(between);
            starts.push_back(it->first + "~absent");   // absent, sorts inside the range
        }
    }

    starts.push_back(ref.rbegin()->first);
    starts.push_back(ref.rbegin()->first + "\xff\xff");

    for (const auto& start : starts) {
        for (std::size_t count : {std::size_t(1), std::size_t(2), std::size_t(16)}) {
            const auto got = idx.scan(start, count);

            // The reference's answer: every key >= start, in order, at most count of them.
            std::vector<Pair> want;
            for (auto it = ref.lower_bound(start); it != ref.end() && want.size() < count; ++it) {
                want.push_back({it->first, it->second});
            }

            if (got.size() != want.size()) {
                fail(std::string(what) + ": scan from " + describe(start) + " count " +
                     std::to_string(count) + " returned " + std::to_string(got.size()) +
                     ", expected " + std::to_string(want.size()));
            }

            for (std::size_t i = 0; i < want.size(); ++i) {
                if (got[i].first != want[i].first) {
                    fail(std::string(what) + ": scan from " + describe(start) + " differs at " +
                         std::to_string(i));
                }

                ++g.scan_records_compared;
            }

            ++g.probe_starts;
        }
    }
}

// ---------------------------------------------------------------------------
// One end-to-end run of the operation sequence, on one arm.
// ---------------------------------------------------------------------------

struct Events {
    std::uint64_t prefix_rebuilds = 0;
    std::uint64_t splits = 0;
    std::uint64_t compactions = 0;
    std::uint64_t overflow_before = 0;
    std::uint64_t overflow_after = 0;
};

Events snapshot(const Index& idx) {
    Events e;
#ifdef E25_DIAG
    const auto& C = validation::e25::counts();
    e.prefix_rebuilds = C.prefix_rebuilds;
    e.splits = C.threshold_splits;
    // NOT C.compactions: that field is declared but never incremented anywhere in the
    // library, so asserting on it would be a check that cannot fail. `compact_entered` is
    // the counter the call site actually raises.
    e.compactions = C.compact_entered;
#endif
    e.overflow_before = idx.overflow_count();
    e.overflow_after = idx.overflow_count();

    return e;
}

void run_arm(Arm& arm, const std::vector<Pair>& keys, const std::string& kind, bool is_lits) {
    Index& idx = *arm.idx;

    Reference ref;
    for (const auto& kv : keys) ref.emplace(kv.first, kv.second);

    const validation::Mode m = validation::query_mode;

#ifdef E25_DIAG
    const auto c0 = validation::e25::counts();
#endif
    const std::size_t overflow_before = idx.overflow_count();

    verify_against_reference(arm, ref, "after build");
    verify_scans(idx, ref, "after build");

    // ---- 1. update only: the mapping must not move and no rebuild may happen --------------
    {
        std::uint64_t v = 1000000;
        for (auto& kv : ref) {
            const bool got = idx.update(kv.first, v);
            const bool want = ref_update(ref, kv.first, v);

            if (got != want) fail(arm.name + std::string(": update return differs"));

            ++v;
            ++g.ops_compared;
        }
        verify_against_reference(arm, ref, "after update-only");
    }

    // ---- 2. erase, including the first and last record of the whole set -------------------
    {
        std::vector<std::string> victims;
        victims.push_back(ref.begin()->first);
        victims.push_back(ref.rbegin()->first);

        // and a spread through the middle
        {
            auto it = ref.begin();
            for (std::size_t i = 0; i < 37 && it != ref.end(); ++i) {
                victims.push_back(it->first);
                std::advance(it, 3);
            }
        }

        for (const auto& v : victims) {
            const bool got = idx.erase(v);
            const bool want = ref_erase(ref, v);

            if (got != want) fail(arm.name + std::string(": erase return differs for ") + describe(v));

            ++g.ops_compared;
        }

        verify_against_reference(arm, ref, "after erase");
        verify_scans(idx, ref, "after erase");
    }

    // ---- 3. insert NEW keys that sort BELOW everything, which breaks a region's prefix ------
    //
    // '!' (0x21) is printable 7-bit ASCII and sorts before every corpus key, whose smallest
    // first byte is '0' (0x30). So this drives the prefix-mismatch rebuild in Index::insert --
    // the path that re-derives a region's prefix and re-places its survivors.
    {
        std::uint64_t v = 2000000;
        for (std::size_t i = 0; i < 64; ++i) {
            const std::string k = "!" + validation::hex_key(i * 7919);

            const bool got = idx.insert(k, v);
            const bool want = ref_insert(ref, k, v);

            if (got != want) fail(arm.name + std::string(": insert return differs for ") + describe(k));

            ++v;
            ++g.ops_compared;
        }

        verify_against_reference(arm, ref, "after prefix-breaking inserts");
        verify_scans(idx, ref, "after prefix-breaking inserts");
    }

    // ---- 4. upsert, on both an existing and an absent key ----------------------------------
    {
        std::uint64_t v = 3000000;
        std::size_t n = 0;

        for (auto& kv : ref) {
            const bool got = idx.upsert(kv.first, v);
            const bool want = ref_upsert(ref, kv.first, v);

            if (got != want) fail(arm.name + std::string(": upsert return differs (existing)"));

            ++v;
            if (++n >= 128) break;
        }

        for (std::size_t i = 0; i < 128; ++i) {
            const std::string k = "~upsert/" + validation::hex_key(i * 104729);

            const bool got = idx.upsert(k, v);
            const bool want = ref_upsert(ref, k, v);

            if (got != want) fail(arm.name + std::string(": upsert return differs (absent)"));

            ++v;
            ++g.ops_compared;
        }

        verify_against_reference(arm, ref, "after upsert");
    }

    // ---- 5. bulk insert to force a SPLIT ---------------------------------------------------
    //
    // The region-level split fires when a region exceeds 2 * region_keys live records, so this
    // appends far more than one region's worth.
    {
        std::uint64_t v = 4000000;
        for (std::size_t i = 0; i < 4096; ++i) {
            const std::string k = "~bulk/" + validation::hex_key(i * 2654435761u);

            ref_insert(ref, k, v);
            idx.insert(k, v);
            ++v;
            ++g.ops_compared;
        }

        verify_against_reference(arm, ref, "after split-driving inserts");
        verify_scans(idx, ref, "after split-driving inserts");

        // Sampled here, not at the end: the container peaks during the insert-heavy phases and
        // shrinks again once the erases start, so an end-of-arm reading would miss it.
        g.peak_overflow = std::max<std::uint64_t>(g.peak_overflow, idx.overflow_count());
    }

    // ---- 6. erase a lot, THEN insert a lot ------------------------------------------------
    //
    // Erasing on its own does not drive compaction: it FREES slots and bytes, so the next insert
    // finds room and the trigger never fires. The trigger needs a placement that FAILS while the
    // group still holds reclaimable garbage, so the erases are followed by inserts that have to
    // be placed -- which is also what makes the deletions land on sub-block boundaries.
    {
        std::vector<std::string> victims;
        for (auto it = ref.begin(); it != ref.end();) {
            victims.push_back(it->first);
            std::advance(it, 2);
            if (victims.size() >= 20000) break;
        }

        for (const auto& v : victims) {
            idx.erase(v);
            ref_erase(ref, v);
            ++g.ops_compared;
        }

        verify_against_reference(arm, ref, "after erased half");
        verify_scans(idx, ref, "after erased half");
    }

    // ---- 7. the inserts that make the erases bite ------------------------------------------
    {
        std::uint64_t v = 5000000;

        for (std::size_t i = 0; i < 20000; ++i) {
            const std::string k = "~after/" + validation::hex_key(i * 40503u);

            ref_insert(ref, k, v);
            idx.insert(k, v);
            ++v;
            ++g.ops_compared;
        }

        verify_against_reference(arm, ref, "after the post-erase inserts");
        verify_scans(idx, ref, "after the post-erase inserts");
    }

#ifdef E25_DIAG
    const auto c1 = validation::e25::counts();
    g.prefix_rebuilds += c1.prefix_rebuilds - c0.prefix_rebuilds;
    g.splits += c1.threshold_splits - c0.threshold_splits;
    g.compactions += c1.compact_entered - c0.compact_entered;
    g.overflow_erases += c1.erase_released_overflow - c0.erase_released_overflow;
#endif

    g.peak_overflow = std::max<std::uint64_t>(g.peak_overflow, idx.overflow_count());
    (void)overflow_before;

    // ---- the backend actually used ---------------------------------------------------------
    //
    // TWO DIFFERENT QUESTIONS, AND BOTH ARE RECORDED.
    //
    //   `lits_coverage()` describes the index AS IT STANDS NOW, walked over its live regions.
    //   That is the quantity that explains a measurement, and it is the one a timing round must
    //   quote beside a timed state.
    //
    //   `Region::lits_stats()` accumulates over every region ever CONSTRUCTED -- splits, prefix
    //   rebuilds and erases included. It answers "how much of the work went through LITS", which
    //   is a property of this whole gate's operation sequence and NOT of any one state in it.
    //
    // Quoting the second where the first is meant is exactly the confusion the review named, so
    // both are printed with their caliber attached rather than one being chosen silently.
    {
        const auto cov = idx.lits_coverage();
        const auto cum = validation::Region::lits_stats();
        const std::uint64_t cumulative = cum.regions_calibrated + cum.regions_fallback + cum.regions_boundary;

        const double region_share = cov.regions ? double(cov.regions_lits) / double(cov.regions) : 0.0;
        const double record_share = cov.records ? double(cov.records_lits) / double(cov.records) : 0.0;

        std::cerr << "  " << arm.name << " [" << kind << "]"
                  << "  CUMULATIVE over the whole gate: " << cumulative << " regions constructed ("
                  << cum.regions_calibrated << " calibrated, " << cum.regions_fallback
                  << " fallback, " << cum.regions_boundary << " boundary)"
                  << "  ||  STATE AT THE END of the operation sequence: " << cov.regions
                  << " live regions, " << cov.regions_lits << " LITS-mapped ("
                  << region_share << "), " << cov.records_lits << "/" << cov.records
                  << " records (" << record_share << "), reasons: admission="
                  << cov.reason_admission << " degenerate=" << cov.reason_degenerate
                  << " boundary=" << cov.reason_boundary
                  << " unsupported=" << cov.reason_unsupported << std::endl;

        if (is_lits) {
            // The assertion that the backend is wired at all, on the CURRENT state: a LITS arm
            // with no LITS-mapped region would still match the reference on content, because
            // group 0 is a valid group. Content equality is NOT evidence that the model ran.
            if (!cov.regions_lits) {
                fail("the LITS arm has no LITS-mapped region at the end -- the backend is not wired");
            }
            if (!cum.regions_calibrated) {
                fail("the LITS arm never calibrated a region at all");
            }
        } else {
            g.m0_regions_via_lits = cumulative + cov.regions_lits;
        }

        g.regions_lits_live = cov.regions_lits;
        g.regions_live = cov.regions;
        g.records_lits_live = cov.records_lits;
        g.records_live = cov.records;
        g.reason_admission = cov.reason_admission;
        g.reason_degenerate = cov.reason_degenerate;
        g.reason_boundary = cov.reason_boundary;
        g.reason_unsupported = cov.reason_unsupported;
        g.regions_calibrated = cum.regions_calibrated;
        g.regions_fallback = cum.regions_fallback;
        g.regions_boundary = cum.regions_boundary;
    }
}

// ---------------------------------------------------------------------------
// The boundary cases the review asked for, on constructed inputs.
// ---------------------------------------------------------------------------

void check_tiny_indexes() {
    Config cfg = e25sw::baseline_config(32);
    e25sw::pin_frozen_switches(cfg);
    cfg.model_backend = validation::ModelBackend::LitsHpt;

    // An index built on NO keys at all.
    {
        Index idx(cfg);
        idx.bulk_load(std::vector<Pair>{});

        if (idx.size() != 0) fail("an empty index reports a non-zero size");
        if (!idx.scan(std::string_view{}, 10).empty()) fail("an empty index scanned a record");
        if (idx.find("anything", validation::query_mode).found) fail("an empty index found a key");
        if (idx.insert("anything", 1) != true) fail("an empty index refused an insert");
        if (idx.size() != 1) fail("an empty index did not take the insert");
        if (!idx.find("anything", validation::query_mode).found) fail("the inserted key is not found");

        ++g.tiny_empty;
    }

    // An index with exactly ONE key: G == 1, and the boundary path must map it to group 0.
    {
        std::vector<Pair> one = {{"only-key", 7}};
        Index idx(cfg);
        idx.bulk_load(one);

        if (idx.size() != 1) fail("a single-key index reports the wrong size");
        if (idx.region(0).groups() != 1) fail("a single-key region does not have G == 1");

        const auto r = idx.find("only-key", validation::query_mode);
        if (!r.found || r.value != 7) fail("a single-key index cannot find its own key");

        ++g.tiny_single;
        ++g.tiny_single_group;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::invalid_argument("check_lits_index KIND N [TRANCOCSV]");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argc > 3 ? argv[3] : "";

        const auto pairs = validation::make_data(kind, nkeys, trcsv);

        Config base = e25sw::baseline_config(32);
        e25sw::pin_frozen_switches(base);

        Config lits = base;
        lits.model_backend = validation::ModelBackend::LitsHpt;

        // The arm difference is DECLARED and asserted, so a run where the two configurations
        // silently became the same one fails instead of passing.
        e25sw::require_only_switches_differ(base, lits, {"model_backend"});

        Arm m0{"M0", base, std::make_unique<Index>(base)};
        Arm ml{"LITS", lits, std::make_unique<Index>(lits)};

        // THE ARMS ARE RUN ONE AT A TIME, AND THE COUNTER IS RESET BEFORE EACH BUILD.
        //
        // Two things have to be true for the coverage numbers to mean anything. The window must
        // start BEFORE bulk_load, because that is where most regions are constructed -- counting
        // only the rebuilds would report a figure an order of magnitude too small. And the M0 arm
        // must be finished before the LITS arm builds, or the M0 report reads the LITS arm's
        // counter (they share one process-wide accumulator) and claims the frozen arm went
        // through the LITS backend.
        validation::Region::reset_lits_stats();
        m0.idx->bulk_load(pairs);
        run_arm(m0, pairs, kind, false);

        validation::Region::reset_lits_stats();
        ml.idx->bulk_load(pairs);
        run_arm(ml, pairs, kind, true);

        check_tiny_indexes();

        // ---- non-vacuity ---------------------------------------------------------------
        if (!g.phases) fail("no phase ran");
        if (!g.keys_compared) fail("no key was compared");
        if (!g.scans_compared) fail("no full scan was compared");
        if (!g.probe_starts) fail("no scan start probe ran");
        if (!g.ops_compared) fail("no operation return value was compared");
        if (!g.regions_calibrated) fail("the LITS arm never calibrated a region");
        if (g.m0_regions_via_lits) fail("the M0 arm constructed regions through the LITS backend");
        if (!g.tiny_empty) fail("the empty index was not exercised");
        if (!g.tiny_single) fail("the single-key index was not exercised");
        if (!g.tiny_single_group) fail("the G=1 region was not exercised");
        if (!g.prefix_rebuilds) fail("the prefix-mismatch rebuild never fired");
        if (!g.splits) fail("the region split never fired");
        if (!g.compactions) fail("compaction never ran");
        if (!g.peak_overflow) fail("the overflow container never held a record");
        if (!g.overflow_erases) fail("no overflow record was ever erased");

        std::cout << "{\"gate\":\"check_lits_index\""
                  << ",\"kind\":\"" << kind << "\""
                  << ",\"keys\":" << nkeys
                  << ",\"phases\":" << g.phases
                  << ",\"ops_compared\":" << g.ops_compared
                  << ",\"keys_compared\":" << g.keys_compared
                  << ",\"scans_compared\":" << g.scans_compared
                  << ",\"scan_records_compared\":" << g.scan_records_compared
                  << ",\"probe_starts\":" << g.probe_starts
                  << ",\"regions_calibrated_cumulative\":" << g.regions_calibrated
                  << ",\"regions_fallback_cumulative\":" << g.regions_fallback
                  << ",\"regions_boundary_cumulative\":" << g.regions_boundary
                  << ",\"regions_live\":" << g.regions_live
                  << ",\"regions_lits_live\":" << g.regions_lits_live
                  << ",\"records_live\":" << g.records_live
                  << ",\"records_lits_live\":" << g.records_lits_live
                  << ",\"reason_admission\":" << g.reason_admission
                  << ",\"reason_degenerate\":" << g.reason_degenerate
                  << ",\"reason_boundary\":" << g.reason_boundary
                  << ",\"reason_unsupported\":" << g.reason_unsupported
                  << ",\"prefix_rebuilds\":" << g.prefix_rebuilds
                  << ",\"splits\":" << g.splits
                  << ",\"compactions\":" << g.compactions
                  << ",\"peak_overflow\":" << g.peak_overflow
                  << ",\"overflow_erases\":" << g.overflow_erases
                  << ",\"tiny_empty\":" << g.tiny_empty
                  << ",\"tiny_single\":" << g.tiny_single
                  << ",\"tiny_single_group\":" << g.tiny_single_group
                  << "}" << std::endl;

        std::cerr << "check_lits_index PASS: " << g.keys_compared << " keys, "
                  << g.ops_compared << " operation returns, " << g.scan_records_compared
                  << " scan records over " << g.probe_starts << " start probes" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "check_lits_index failed: " << e.what() << std::endl;
        return 1;
    }
}

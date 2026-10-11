#!/usr/bin/env python3
"""Stage D supplementary instrumentation (2026-10-10), applied once to index.hpp.

WHAT AND WHY. The stage-D entry diagnosis (RESULTS_WRITE_RESIDUAL_20261010.md,
generation 1) found that one insert re-derives the same sub-block index several
times: find_impl's locate_subblock computes it and DROPS it, put_primary walks to
recompute it, first_candidate walks to recompute it again, and the post-repack
retry walks once more. The plan allows ONE bounded supplementary check, and this
is it: counters that price the walks and an equality probe that measures the
claim the future candidate depends on (the directory answer == first_candidate's
choice) on the real workload instead of assuming it.

Everything added here is diagnostic-only:
  * the counters are E25_DIAG (compiled out of every timing build);
  * the two clocks are E30_WRITE (present only in the timing build, and their
    own overhead is reported through the same calibration as every other phase).

The patch FAILS LOUDLY if an anchor is missing or already patched, so it cannot
half-apply. It touches index.hpp only.
"""
import hashlib
import sys

PATH = "index.hpp"

REPLACEMENTS = []


def rep(old, new, note):
    REPLACEMENTS.append((old, new, note))


rep(
    """    std::uint64_t scan_out_bytes = 0;        // full-key bytes materialised (prefix+suffix)
    void reset() { *this = Counts{}; }""",
    """    std::uint64_t scan_out_bytes = 0;        // full-key bytes materialised (prefix+suffix)
    // ---- Stage D supplementary check (2026-10-10) --------------------------
    // How much of the placement path is RE-DERIVATION of the sub-block index that
    // find_impl's locate_subblock already computed for this very key and dropped.
    // `put_primary_calls` and the two first_candidate counters price the walks;
    // the hint_* family measures whether the directory answer really equals the
    // block first_candidate picks -- the equality the candidate would rely on.
    std::uint64_t put_primary_calls = 0;   // put_primary entries (one or more per insert)
    std::uint64_t fc_calls = 0;            // first_candidate entries
    std::uint64_t fc_probes = 0;           // ... its boundary-compare iterations
    std::uint64_t fc_gap = 0;              // ... of which took the two-block (prev, i) path
    std::uint64_t hint_eq = 0;             // directory answer == first_candidate's block
    std::uint64_t hint_ne = 0;             // ... they disagreed (must stay 0)
    std::uint64_t hint_nodir = 0;          // no directory: no answer to compare
    std::uint64_t hint_below = 0;          // directory says below every primary key
    void reset() { *this = Counts{}; }""",
    "e25 counters",
)

rep(
    """    std::uint64_t w_findloc_ns = 0, w_findloc_calls = 0;     // Region::find_located""",
    """    std::uint64_t w_findloc_ns = 0, w_findloc_calls = 0;     // Region::find_located
    // Stage D supplementary check: the two walks the placement path re-runs. Named
    // so the analyzer can read the re-derivation's price directly instead of
    // bounding it by place-without-repack.
    std::uint64_t w_walk_ns = 0, w_walk_calls = 0;           // put_primary's sub-block walk
    std::uint64_t w_fc_ns = 0, w_fc_calls = 0;               // first_candidate""",
    "e30 timers",
)

rep(
    """    bool put_primary(std::uint32_t g, std::string_view s, std::uint64_t value, std::uint8_t tag) {
        const std::size_t nb = subblocks(g);""",
    """    bool put_primary(std::uint32_t g, std::string_view s, std::uint64_t value, std::uint8_t tag) {
        E25_INC(put_primary_calls);
        E30_W(walk);
        const std::size_t nb = subblocks(g);""",
    "put_primary instrumentation",
)

rep(
    """    std::size_t first_candidate(std::uint32_t g, std::string_view s) const {
        const std::size_t nb = subblocks(g);
        std::size_t prev = SIZE_MAX;
        for (std::size_t i = 0; i < nb; ++i) {
            const auto& h = header(g, i);""",
    """    std::size_t first_candidate(std::uint32_t g, std::string_view s) const {
        E25_INC(fc_calls);
        E30_W(fc);
        const std::size_t nb = subblocks(g);
        std::size_t prev = SIZE_MAX;
        for (std::size_t i = 0; i < nb; ++i) {
            E25_INC(fc_probes);
            const auto& h = header(g, i);""",
    "first_candidate instrumentation",
)

rep(
    """            if (prev != SIZE_MAX && compare(s, suffix(g, i, es[0])) < 0) return prev;
            return i;
        }
        return prev == SIZE_MAX ? 0 : prev;""",
    """            if (prev != SIZE_MAX && compare(s, suffix(g, i, es[0])) < 0) {
                E25_INC(fc_gap);   // the two-block path: put_primary tries prev, then i
                return prev;
            }
            return i;
        }
        if (prev == SIZE_MAX) return 0;
        E25_INC(fc_gap);           // s above every block: the append path
        return prev;""",
    "first_candidate gap counter",
)

rep(
    """                const std::size_t rank_blk = first_candidate(loc.group, s);""",
    """                const std::size_t rank_blk = first_candidate(loc.group, s);
#ifdef E25_DIAG
                {
                    // Stage D supplementary check: would the SEARCH's own directory
                    // answer have chosen this same block? The candidate threads that
                    // answer through Located instead of recomputing it; this measures
                    // the equality on the real workload rather than assuming it.
                    if (!has_dir(loc.group)) {
                        E25_INC(hint_nodir);
                    } else {
                        Result tmp;
                        const std::size_t dir_cand = locate_subblock(loc.group, s, tmp);
                        if (dir_cand == NO_SUBBLOCK) E25_INC(hint_below);
                        else if (dir_cand == rank_blk) E25_INC(hint_eq);
                        else E25_INC(hint_ne);
                    }
                }
#endif""",
    "hint equality probe",
)


def main():
    with open(PATH, encoding="utf-8") as fh:
        text = fh.read()
    before = hashlib.sha256(text.encode("utf-8")).hexdigest()
    for old, new, note in REPLACEMENTS:
        if new in text:
            print("ALREADY PATCHED (%s); refusing to apply twice" % note, file=sys.stderr)
            sys.exit(2)
        if text.count(old) != 1:
            print("ANCHOR NOT FOUND exactly once (%s): %d occurrences" %
                  (note, text.count(old)), file=sys.stderr)
            sys.exit(2)
        text = text.replace(old, new, 1)
        print("patched: %s" % note)
    with open(PATH, "w", encoding="utf-8") as fh:
        fh.write(text)
    after = hashlib.sha256(text.encode("utf-8")).hexdigest()
    print("index.hpp sha256 %s -> %s" % (before, after))
    print("index.hpp lines: %d" % text.count(chr(10)))


if __name__ == "__main__":
    main()

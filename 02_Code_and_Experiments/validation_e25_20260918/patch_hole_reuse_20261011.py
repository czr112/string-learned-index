#!/usr/bin/env python3
"""Candidate H patch (2026-10-11): primary_hole_reuse, per NEXT_ACTION_PLAN_20261011 section 4.

Edits, all assertion-checked:
  * index.hpp     -- Config field, six branch counters, the insert_into byte-refusal hook,
                     and insert_into_hole itself (same-block dead-range placement)
  * switches.hpp  -- the candidate registered in all FOUR tables (baseline_config,
                     pin_frozen_switches, switches(), require_only_switches_differ)
  * tools_build.py -- check_hole_reuse + san_check_hole_reuse targets (src/flags/objects)
  * check_hole_reuse.cpp -- the gate source, written here so it keeps LF endings

Pre-hashes are the repack-split identities (the round this candidate is built on); every
anchor must occur exactly once or the patch refuses.
"""
import hashlib
import sys
from pathlib import Path

DIR = Path(__file__).resolve().parent


def sha(b):
    return hashlib.sha256(b).hexdigest()


def edit(name, pre_sha, subs):
    p = DIR / name
    b = p.read_bytes()
    d = sha(b)
    if d != pre_sha:
        print("REFUSED %s: on-disk %s != expected pre-hash %s" % (name, d, pre_sha),
              file=sys.stderr)
        sys.exit(2)
    for i, (old, new) in enumerate(subs, 1):
        ob, nb = old.encode("utf-8"), new.encode("utf-8")
        n = b.count(ob)
        if n != 1:
            print("REFUSED %s: anchor %d occurs %d times, not once" % (name, i, n),
                  file=sys.stderr)
            sys.exit(2)
        b = b.replace(ob, nb)
    p.write_bytes(b)
    print("%s: %s -> %s" % (name, d[:12], sha(b)))


INDEX_PRE = "0d256611be49d902d411c82f22c2857cea2ac92e43bb67514b3126ee4a39d072"
SWITCHES_PRE = "46a18941445ab7bbba9db376ef911d911fb79eac258d85dee23ffd5abd9f44db"
TOOLS_PRE = "53b9cbbf81be3465b529be2f9c83933c81e8229e725d2191da67d2bb685ca569"

INDEX_CONFIG = """
    // -----------------------------------------------------------------------
    // Primary-hole reuse (candidate H, default OFF).
    //
    // When an insert's contiguous space is short, the shipped path repacks the target
    // sub-block (subblock_reclaim) and retries. The bounded entry round measured that pass
    // at 40.1%/40.8% move+rewrite (mixed@200%, ctx16/lits), 96.7% of its memmove bytes
    // real relocations, ~4.6 KB per pass (RESULTS_REPACK_SPLIT_20261011.md). This candidate
    // instead finds a DEAD RANGE in the SAME sub-block and writes the new suffix there,
    // moving nothing else.
    //
    // Requires subblock_reclaim (the mechanism it substitutes for), never touches blocks
    // under two live records, and is inert when off. See insert_into_hole for the scope
    // locks.
    bool primary_hole_reuse = false;
"""

INDEX_COUNTERS = """    std::uint64_t rp_selfcopy_bytes = 0;
    // ---- Candidate H (primary_hole_reuse, 2026-10-11): the hole-placement branches -----
    // The bounded entry round justified ONE minimal candidate; these count its behaviour so
    // the gate can assert BOTH branches fired and the round can read how many repacks it
    // replaces instead of assuming it. counted under E25_DIAG only, like every counter here.
    std::uint64_t hole_reuse_attempts = 0;      // slot OK, bytes short, H applies
    std::uint64_t hole_reuse_placed = 0;        // ... and a dead range took the suffix
    std::uint64_t hole_reuse_absent = 0;        // ... and no gap was large enough
    std::uint64_t hole_reuse_skipped_lt2 = 0;   // < 2 live records: low-activity territory
    std::uint64_t hole_reuse_skipped_wide = 0;  // > 48 records: over the fixed stack bound
    std::uint64_t hole_reuse_bytes = 0;         // suffix bytes written into dead ranges
"""

INDEX_HOOK = """        if (s.size() > h.bottom - start_of(g)) {
            E25_INC(place_fail_bytes);
#ifdef E25_DIAG
            diag_note_byte_failure(g, i, s.size());
#endif
            // Candidate H (primary_hole_reuse): before the caller's repack machinery runs,
            // try the block's own dead ranges. On failure this returns false exactly as the
            // original code did, with the block untouched.
            if (config_.primary_hole_reuse && config_.subblock_reclaim &&
                insert_into_hole(g, i, s, value, tag))
                return true;
            return false;
        }
"""

INDEX_FUNCTION = """        // A front insert becomes this sub-block's new minimum.
        if (p == 0 && !suppress_dir_rebuild) rebuild_dir(g);
        return true;
    }

    // Candidate H (primary_hole_reuse, default OFF): place the suffix into a DEAD RANGE of
    // the SAME sub-block instead of repacking the block for contiguous room.
    //
    // H does not touch existing suffixes -- it finds a gap between them, writes the new
    // suffix there, inserts the descriptor in KEY order and leaves h.bottom where it was.
    // The bytes H consumes come from a dead range, not from the contiguous region below
    // h.bottom, so the block's occupancy does not grow.
    //
    // Scope is FIXED and enforced here: same block only; at least two live records; at most
    // 48 (a fixed stack bound, not a tuned parameter); the first fitting gap scanning
    // PHYSICALLY from high to low; no persistent hole table, no new header field, nothing
    // cached across calls. On failure nothing is modified and the caller's original control
    // flow continues.
    bool insert_into_hole(std::uint32_t g, std::size_t i, std::string_view s,
                          std::uint64_t value, std::uint8_t tag) {
        auto& h = header(g, i);
        if (h.count >= slots_of(g)) return false;   // slots come first: H creates none
        if (h.count < 2) { E25_INC(hole_reuse_skipped_lt2); return false; }
        if (h.count > 48) { E25_INC(hole_reuse_skipped_wide); return false; }
        E25_INC(hole_reuse_attempts);

        const auto* es = entries(g, i);
        const std::size_t n = h.count;
        std::size_t order[48];
        for (std::size_t j = 0; j < n; ++j) order[j] = j;
        for (std::size_t a = 1; a < n; ++a) {
            // The same descending-by-offset insertion sort repack_subblock uses; the
            // physical order is NOT assumed to match the descriptor order.
            const std::size_t key = order[a];
            const std::uint32_t koff = es[key].offset;
            std::size_t b = a;
            while (b > 0 && es[order[b - 1]].offset < koff) { order[b] = order[b - 1]; --b; }
            order[b] = key;
        }

        // Gaps, from the top of the block downward. A gap [low, cur) sits above the record
        // whose top is `low`; the new suffix goes at the gap's LOW end, so it never crosses
        // bb_ and whatever remains of the gap stays directly above it.
        std::size_t pos = SIZE_MAX;
        std::size_t cur = bb_[g];
        for (std::size_t a = 0; a < n; ++a) {
            const auto& e = es[order[a]];
            const std::size_t low = std::size_t(e.offset) + e.length;
            if (cur >= low && cur - low >= s.size()) { pos = low; break; }
            cur = e.offset;
        }
        if (pos == SIZE_MAX && cur >= h.bottom && cur - h.bottom >= s.size())
            pos = h.bottom;                          // the gap below the lowest record
        if (pos == SIZE_MAX) { E25_INC(hole_reuse_absent); return false; }

        auto* esw = entries(g, i);
        std::size_t p = 0;
        while (p < h.count && E25_CMP(suffix(g, i, esw[p]), s) < 0) ++p;
        E18_ADD(entry_shifts, h.count - p);
        for (std::size_t j = h.count; j > p; --j) {
            esw[j] = esw[j - 1];
            h.tags[j] = h.tags[j - 1];
        }
        if (!s.empty()) std::memcpy(base_of(g, i) + pos, s.data(), s.size());
        esw[p] = {static_cast<std::uint32_t>(pos), static_cast<std::uint32_t>(s.size()), value};
        h.tags[p] = tag;
        ++h.count;
        if (p == 0 && !suppress_dir_rebuild) rebuild_dir(g);
        E25_INC(hole_reuse_placed);
        E25_ADD(hole_reuse_bytes, s.size());
        return true;
    }
"""

INDEX_SUBS = [
    ("    bool reclaim_low_activity = false;\n",
     "    bool reclaim_low_activity = false;\n" + INDEX_CONFIG),
    ("    std::uint64_t rp_selfcopy_bytes = 0;\n", INDEX_COUNTERS),
    ("""        if (s.size() > h.bottom - start_of(g)) {
            E25_INC(place_fail_bytes);
#ifdef E25_DIAG
            diag_note_byte_failure(g, i, s.size());
#endif
            return false;
        }
""", INDEX_HOOK),
    ("""        // A front insert becomes this sub-block's new minimum.
        if (p == 0 && !suppress_dir_rebuild) rebuild_dir(g);
        return true;
    }
""", INDEX_FUNCTION),
]

SWITCHES_SUBS = [
    ("    c.reclaim_low_activity = false;     // structural candidate, not in the frozen set\n",
     "    c.reclaim_low_activity = false;     // structural candidate, not in the frozen set\n"
     "    c.primary_hole_reuse = false;       // structural candidate, not in the frozen set\n"),
    ("    c.reclaim_low_activity = false;    // NOT part of the frozen set; the n<2 reclaim candidate\n",
     "    c.reclaim_low_activity = false;    // NOT part of the frozen set; the n<2 reclaim candidate\n"
     "    c.primary_hole_reuse = false;      // NOT part of the frozen set; candidate H (2026-10-11)\n"),
    ('           ",reclaim_low_activity=" + (c.reclaim_low_activity ? "1" : "0") +\n',
     '           ",reclaim_low_activity=" + (c.reclaim_low_activity ? "1" : "0") +\n'
     '           ",primary_hole_reuse=" + (c.primary_hole_reuse ? "1" : "0") +\n'),
    ("        {\"reclaim_low_activity\", a.reclaim_low_activity, b.reclaim_low_activity},\n",
     "        {\"reclaim_low_activity\", a.reclaim_low_activity, b.reclaim_low_activity},\n"
     "        {\"primary_hole_reuse\", a.primary_hole_reuse, b.primary_hole_reuse},\n"),
]

TOOLS_SUBS = [
    ('    "san_check_low_activity",\n',
     '    "san_check_low_activity",\n'
     '    # Candidate H (2026-10-11): the hole-placement path writes suffixes into dead ranges\n'
     '    # and moves descriptors -- new code on the write side, so the sanitizer set first.\n'
     '    "san_check_hole_reuse",\n'),
    ('    "check_low_activity": ["-DE25_DIAG"],\n',
     '    "check_low_activity": ["-DE25_DIAG"],\n'
     '    # Candidate H gate: asserts the branch counters are non-zero and reads the hole\n'
     '    # counters beside the repack counters, which needs the counters. Not a timing build.\n'
     '    "check_hole_reuse": ["-DE25_DIAG"],\n'
     '    "san_check_hole_reuse": ["-DE25_DIAG"],\n'),
    ('        "san_check_low_activity": ["build/art.o"],\n',
     '        "san_check_low_activity": ["build/art.o"],\n'
     '        "check_hole_reuse": ["build/art.o"],\n'
     '        "san_check_hole_reuse": ["build/art.o"],\n'),
    ('       "san_check_low_activity": "check_low_activity.cpp",\n',
     '       "san_check_low_activity": "check_low_activity.cpp",\n'
     '       # Candidate H: one source, two targets -- the gate and its sanitizer twin.\n'
     '       "san_check_hole_reuse": "check_hole_reuse.cpp",\n'),
]
GATE_SRC = r'''// Gate for the primary-hole-reuse candidate (H, Config::primary_hole_reuse).
//
// TWO THINGS HAVE TO BE TRUE, AND THEY ARE CHECKED SEPARATELY.
//
// 1. IT MUST BE CORRECT. H writes a new suffix into a DEAD RANGE of the same sub-block
//    instead of repacking the block for contiguous room, and it moves descriptors exactly
//    as insert_into does. That touches the layout every read path walks, so the gate drives
//    a W index and a W+H index through the SAME operation sequence and compares them
//    against an independent std::map after every phase: values, size, find and scans from
//    fixed and random starts.
//
// 2. IT MUST ACTUALLY RUN AND ACTUALLY SAVE THE REPACK. A branch that never fired makes
//    every comparison vacuous (the G0 audit found exactly that hole once). The gate asserts
//    the byte-refusal hook ran at all, that attempts split into placed+absent, and that
//    BOTH outcomes occurred on the main workload -- otherwise this run is reported as
//    NOT EXERCISED rather than green. The saving claim is read from the counters beside it:
//    the number of block repacks, the repack calls and the moved bytes must not INCREASE on
//    the H arm, measured on the same sequence run sequentially so the process-global
//    counters belong to one arm at a time.
//
// CASES THE PLAN NAMES, and how each is reached:
//   hole exactly fits / slack / multiple / none   the replacement workload (random victims,
//                                                 fresh keys anchored on their victim)
//   full slots / byte refusal                     this workload refuses placements on both
//   empty block / single record / zero-length suffix   small-corpus block: keys equal to
//                                                 the region prefix have zero-length suffixes
//   delete/reinsert, duplicate insert             live-key cases taken from the reference
//   long/high-bit/NUL keys                        ctx16-only synthetic block (the LITS
//                                                 domain contract is tested by its own gate)
//   H + overflow_migrate                          combination block: H can rescue a drain
//                                                 insert the reservation thought impossible,
//                                                 and that must not lose or duplicate a record
//
// ASan/UBSan is a separate target (san_check_hole_reuse) over this same source.
//
// Usage:  check_hole_reuse KIND N TRANCOCSV BACKEND      (BACKEND = ctx16 | lits)

#include "data.hpp"
#include "e9_alloc.hpp"
#include "index.hpp"
#include "selected.hpp"
#include "switches.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace validation;

#ifndef E25_DIAG
#error "check_hole_reuse must be built with -DE25_DIAG"
#endif

namespace {

std::uint64_t checks = 0;
std::uint64_t failures = 0;

// Set when the workload never reached one of the candidate's two outcomes. Distinct from
// `failures` on purpose: an unexercised branch is a gap in coverage, not evidence of a
// defect, and the two need different responses.
bool not_exercised = false;

void require(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cout << "FAIL: " << what << "\n";
    }
}

struct UByteLess {
    bool operator()(std::string_view a, std::string_view b) const {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](char x, char y) {
                                                return static_cast<unsigned char>(x) <
                                                       static_cast<unsigned char>(y);
                                            });
    }
};

using Reference = std::map<std::string, std::uint64_t, UByteLess>;

// W = the frozen baseline with subblock_reclaim + reclaim_widest, navigation and scan
// candidates off -- the same arm the entry round measured. H is the single varying field.
Config arm_config(std::uint32_t thr, bool hole, bool migrate, bool lits) {
    Config c = e25sw::baseline_config(thr);
    e25sw::pin_frozen_switches(c);
    c.subblock_reclaim = true;      // W: the mechanism H substitutes for
    c.reclaim_widest = true;        // W
    c.reclaim_low_activity = false;
    c.compact_region_routing = false;
    c.overflow_migrate = migrate;
    c.primary_hole_reuse = hole;
    if (lits) c.model_backend = ModelBackend::LitsHpt;
    return c;
}

const Mode kMode{Route::Interval, Prefetch::Off, true, true, OvSearch::Hybrid, 32, false};

// Compares the two indexes and the reference on the OBSERVABLE, not on the layout. H is
// allowed to move suffixes; it is not allowed to change an answer.
void compare_all(const Index& a, const Index& b, const Reference& ref, const std::string& label) {
    require(a.size() == b.size(), label + ": the two indexes hold the same number of keys");
    require(a.size() == ref.size(), label + ": and it is the number the reference holds");

    std::uint64_t mismatches = 0;
    for (const auto& kv : ref) {
        const Result ra = a.find(kv.first, kMode);
        const Result rb = b.find(kv.first, kMode);
        if (!ra.found || !rb.found || ra.value != kv.second || rb.value != kv.second)
            ++mismatches;
    }
    require(mismatches == 0, label + ": every live key answers the same in all three");

    std::vector<std::string> starts;
    starts.push_back(std::string());
    starts.push_back("\x01");
    std::size_t taken = 0;
    for (const auto& kv : ref) {
        if (taken % std::max<std::size_t>(1, ref.size() / 16) == 0) starts.push_back(kv.first);
        if (++taken > 32) break;
    }
    for (const auto& start : starts) {
        const auto sa = a.scan(start, 64);
        const auto sb = b.scan(start, 64);
        require(sa.size() == sb.size(), label + ": scan agrees on the row count");
        for (std::size_t i = 0; i < std::min(sa.size(), sb.size()); ++i)
            require(sa[i].first == sb[i].first && sa[i].second == sb[i].second,
                    label + ": scan agrees row by row");
    }

    require(!a.find(std::string("~absent~", 8), kMode).found,
            label + ": an absent key misses on the baseline");
    require(!b.find(std::string("~absent~", 8), kMode).found,
            label + ": an absent key misses on the candidate");
}

struct Op {
    std::string victim;
    std::string fresh;
    std::uint64_t value;
};

std::vector<Op> make_ops(const std::vector<Pair>& keys, std::size_t rounds) {
    std::vector<std::size_t> order(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) order[i] = i;
    std::mt19937_64 rng(20261011);
    std::shuffle(order.begin(), order.end(), rng);

    std::vector<Op> ops;
    const std::size_t n = std::min<std::size_t>(keys.size(), rounds);
    ops.reserve(n);
    std::uint64_t serial = 0;
    for (std::size_t i = 0; i < n; ++i) {
        Op op;
        op.victim = keys[order[i]].first;
        std::string base = op.victim;
        const auto pos = base.find("~f");
        if (pos != std::string::npos) base.resize(pos);
        op.fresh = base + "~f" + std::to_string(serial++);
        op.value = 0x5000ULL + serial;
        ops.push_back(std::move(op));
    }
    return ops;
}

void apply_ops(Index& s, Reference& ref, const std::vector<Op>& ops) {
    for (const Op& op : ops) {
        s.erase(op.victim);
        ref.erase(op.victim);
        s.insert(op.fresh, op.value);
        ref.emplace(op.fresh, op.value);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 5) throw std::invalid_argument("check_hole_reuse KIND N TRANCOCSV BACKEND");

        const std::string kind = argv[1];
        const std::size_t nkeys = std::stoull(argv[2]);
        const std::string trcsv = argv[3];
        const std::string backend = argv[4];
        const bool lits = (backend == "lits");
        if (!lits && backend != "ctx16")
            throw std::invalid_argument("BACKEND must be ctx16 or lits");

        const auto keys = make_data(kind, nkeys, trcsv);

        const Config off = arm_config(32, false, false, lits);
        Config on = off;
        on.primary_hole_reuse = true;

        auto a = std::make_unique<Index>(off);
        auto b = std::make_unique<Index>(on);
        a->bulk_load(keys);
        b->bulk_load(keys);

        Reference ref;
        for (const auto& kv : keys) ref.emplace(kv.first, kv.second);

        compare_all(*a, *b, ref, "after bulk_load");
        require(a->region_count() == b->region_count(),
                "the switch does not change the region count");

        // ------------------------------------------------------------------ replacement
        // A REPLACEMENT, not an erase: an erase only frees space and never fails a
        // placement, so the byte-refusal hook would never be entered.
        const std::vector<Op> ops = make_ops(keys, 40000);
        {
            Reference r = ref;
            for (const Op& op : ops) {
                a->erase(op.victim);
                b->erase(op.victim);
                r.erase(op.victim);
                a->insert(op.fresh, op.value);
                b->insert(op.fresh, op.value);
                r.emplace(op.fresh, op.value);
            }
            compare_all(*a, *b, r, "after replacement");
        }

        // ------------------------------------------------------------------ counters
        // The counters are process-global, so the saving claim runs the SAME sequence on a
        // W index and then on a W+H index, sequentially: one arm owns the counters at a time.
        std::uint64_t w_reclaims = 0, w_calls = 0, w_moved = 0;
        std::uint64_t h_reclaims = 0, h_calls = 0, h_moved = 0;
        std::uint64_t att = 0, placed = 0, absent = 0, lt2 = 0, wide = 0, hbytes = 0;
        {
            auto c = std::make_unique<Index>(off);
            c->bulk_load(keys);
            Reference r;
            for (const auto& kv : keys) r.emplace(kv.first, kv.second);
            validation::e25::counts().reset();
            apply_ops(*c, r, ops);
            w_reclaims = validation::e25::counts().subblock_reclaims;
            w_calls = validation::e25::counts().rp_calls;
            w_moved = validation::e25::counts().rp_moved_bytes;

            auto d = std::make_unique<Index>(on);
            d->bulk_load(keys);
            Reference r2;
            for (const auto& kv : keys) r2.emplace(kv.first, kv.second);
            validation::e25::counts().reset();
            apply_ops(*d, r2, ops);
            h_reclaims = validation::e25::counts().subblock_reclaims;
            h_calls = validation::e25::counts().rp_calls;
            h_moved = validation::e25::counts().rp_moved_bytes;
            att = validation::e25::counts().hole_reuse_attempts;
            placed = validation::e25::counts().hole_reuse_placed;
            absent = validation::e25::counts().hole_reuse_absent;
            lt2 = validation::e25::counts().hole_reuse_skipped_lt2;
            wide = validation::e25::counts().hole_reuse_skipped_wide;
            hbytes = validation::e25::counts().hole_reuse_bytes;
            compare_all(*c, *d, r2, "after the counter arms' replacement");
        }
        require(att > 0, "the H byte-refusal hook ran at all on this workload");
        require(att == placed + absent, "attempts split into placed + absent");
        require(h_reclaims <= w_reclaims, "H does not increase the number of sub-block repacks");
        require(h_calls <= w_calls, "H does not increase the number of repack passes");
        require(h_moved <= w_moved, "H does not increase the bytes repack moves");

        if (placed == 0 || absent == 0) {
            std::cout << "  NOT EXERCISED: placed=" << placed << " absent=" << absent
                      << " -- this run does not cover both outcomes\n";
            not_exercised = true;
        } else {
            std::cout << "  H placed " << placed << " of " << att
                      << " attempts (" << hbytes << " B); repacks " << w_reclaims << " -> "
                      << h_reclaims << ", moved bytes " << w_moved << " -> " << h_moved
                      << "\n";
        }

        // ------------------------------------------------------------------ small corpora
        // Empty suffix (key == region prefix), delete-to-one-record, and the refill. The
        // block geometry is forced with region_keys = 2 so the small corpus produces
        // several sub-blocks.
        {
            std::vector<Pair> small = {
                {std::string("pre"), 1},   {std::string("pre0"), 2}, {std::string("pre1"), 3},
                {std::string("pre2"), 4},  {std::string("pre3"), 5}, {std::string("pre4"), 6},
                {std::string("pre5"), 7},  {std::string("pre6"), 8},
            };
            Config o2 = arm_config(8, false, false, lits);
            Config c2 = o2;
            c2.primary_hole_reuse = true;
            o2.region_keys = 2;
            c2.region_keys = 2;
            auto sa = std::make_unique<Index>(o2);
            auto sb = std::make_unique<Index>(c2);
            sa->bulk_load(small);
            sb->bulk_load(small);
            Reference r2;
            for (const auto& kv : small) r2.emplace(kv.first, kv.second);

            for (std::size_t i = 1; i < small.size(); ++i) {
                sa->erase(small[i].first);
                sb->erase(small[i].first);
                r2.erase(small[i].first);
            }
            compare_all(*sa, *sb, r2, "small: drained to one record");

            for (std::size_t i = 0; i < 12; ++i) {
                const std::string k = "pre9~f" + std::to_string(i);
                sa->insert(k, 100 + i);
                sb->insert(k, 100 + i);
                r2.emplace(k, 100 + i);
            }
            compare_all(*sa, *sb, r2, "small: refilled");

            const std::string lone = r2.begin()->first;
            sa->erase(lone);
            sb->erase(lone);
            r2.erase(lone);
            compare_all(*sa, *sb, r2, "small: deleted down to empty");
        }

        // ------------------------------------------------------------------ delete/reinsert + duplicate
        {
            const std::string k = ref.begin()->first;
            a->erase(k);
            b->erase(k);
            ref.erase(k);
            compare_all(*a, *b, ref, "after deleting a live key");

            a->insert(k, 777);
            b->insert(k, 777);
            ref.emplace(k, 777);
            compare_all(*a, *b, ref, "after reinserting it");

            const std::string k2 = ref.begin()->first;
            const std::uint64_t old = ref.begin()->second;
            const std::size_t before = ref.size();
            const bool ia = a->insert(k2, 999);
            const bool ib = b->insert(k2, 999);
            require(!ia && !ib, "a duplicate insert is refused in both");
            require(ref.size() == before, "the test did not corrupt its own reference");
            compare_all(*a, *b, ref, "after a refused duplicate insert");
            require(a->find(k2, kMode).value == old && b->find(k2, kMode).value == old,
                    "a duplicate insert kept the old value in both");
        }

        // ------------------------------------------------------------------ ctx16-only keys
        if (!lits) {
            std::vector<Pair> edge;
            edge.push_back({std::string("pre"), 1});
            edge.push_back({std::string("pre") + std::string(1, '\0') + "a", 2});
            edge.push_back({std::string("pre") + std::string("\xff\xfe", 2), 3});
            edge.push_back({std::string("pre") + std::string(4090, 'L'), 4});
            edge.push_back({std::string("pre~s"), 5});
            Config o3 = arm_config(8, false, false, lits);
            Config c3 = o3;
            c3.primary_hole_reuse = true;
            o3.region_keys = 2;
            c3.region_keys = 2;
            auto sa = std::make_unique<Index>(o3);
            auto sb = std::make_unique<Index>(c3);
            sa->bulk_load(edge);
            sb->bulk_load(edge);
            Reference r3;
            for (const auto& kv : edge) r3.emplace(kv.first, kv.second);
            compare_all(*sa, *sb, r3, "edge keys: NUL, high bytes, 4092-byte key");
            for (int i = 0; i < 24; ++i) {
                const std::string k = std::string("pre~f") + std::to_string(i);
                sa->insert(k, 200 + i);
                sb->insert(k, 200 + i);
                r3.emplace(k, 200 + i);
            }
            compare_all(*sa, *sb, r3, "edge keys: after inserts");
        } else {
            std::cout << "  LITS domain edge keys are covered by check_lits_index; skipped "
                         "here by design\n";
        }

        // ------------------------------------------------------------------ H + overflow_migrate
        // Semantics only: H can rescue a drain insert that its virtual reservation thought
        // impossible, and that must not lose or duplicate a record.
        {
            Config o4 = arm_config(32, false, true, lits);
            Config c4 = o4;
            c4.primary_hole_reuse = true;
            auto sa = std::make_unique<Index>(o4);
            auto sb = std::make_unique<Index>(c4);
            const auto small_keys = make_data(kind, std::min<std::size_t>(nkeys, 16384), trcsv);
            sa->bulk_load(small_keys);
            sb->bulk_load(small_keys);
            Reference r4;
            for (const auto& kv : small_keys) r4.emplace(kv.first, kv.second);
            const std::vector<Op> ops4 = make_ops(small_keys, 12000);
            for (const Op& op : ops4) {
                sa->erase(op.victim);
                sb->erase(op.victim);
                r4.erase(op.victim);
                sa->insert(op.fresh, op.value);
                sb->insert(op.fresh, op.value);
                r4.emplace(op.fresh, op.value);
            }
            compare_all(*sa, *sb, r4, "combination H + overflow_migrate");
        }

        // ------------------------------------------------------------------ report
        std::cout << "{\"kind\":\"" << kind << "\",\"backend\":\"" << backend
                  << "\",\"keys\":" << keys.size()
                  << ",\"final_size\":" << b->size()
                  << ",\"regions\":" << b->region_count()
                  << ",\"h_attempts\":" << att
                  << ",\"h_placed\":" << placed
                  << ",\"h_absent\":" << absent
                  << ",\"h_skipped_lt2\":" << lt2
                  << ",\"h_skipped_wide\":" << wide
                  << ",\"h_bytes\":" << hbytes
                  << ",\"w_reclaims\":" << w_reclaims
                  << ",\"h_reclaims\":" << h_reclaims
                  << ",\"w_moved_bytes\":" << w_moved
                  << ",\"h_moved_bytes\":" << h_moved
                  << ",\"checks\":" << checks
                  << ",\"failures\":" << failures
                  << "}" << std::endl;

        if (failures != 0) {
            std::cout << failures << " FAILED out of " << checks << "\n";
            return 1;
        }
        if (not_exercised) {
            std::cout << checks << "/" << checks
                      << " checks passed, but one of the candidate's outcomes was NOT "
                         "EXERCISED on this corpus\n";
            return 3;
        }
        std::cout << checks << "/" << checks << " checks passed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
'''


def expect(name, needle):
    b = (DIR / name).read_bytes()
    if needle.encode() not in b:
        print("POST-CHECK FAILED: %s lacks %r" % (name, needle), file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    gp = DIR / "check_hole_reuse.cpp"
    if gp.exists():
        print("REFUSED: check_hole_reuse.cpp already exists", file=sys.stderr)
        sys.exit(2)
    edit("index.hpp", INDEX_PRE, INDEX_SUBS)
    edit("switches.hpp", SWITCHES_PRE, SWITCHES_SUBS)
    edit("tools_build.py", TOOLS_PRE, TOOLS_SUBS)
    gp.write_text(GATE_SRC, encoding="utf-8", newline="\n")
    print("check_hole_reuse.cpp: written, %s" % sha(gp.read_bytes())[:12])
    expect("index.hpp", "insert_into_hole")
    expect("index.hpp", "hole_reuse_placed")
    expect("switches.hpp", "primary_hole_reuse")
    expect("tools_build.py", "san_check_hole_reuse")
    expect("check_hole_reuse.cpp", "check_hole_reuse KIND N TRANCOCSV BACKEND")
    print("patch_hole_reuse_20261011: OK")
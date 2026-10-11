#!/usr/bin/env python3
"""E25 build/run closure (extended from the E8..E24 one).

Carries over the two guarantees the earlier rounds relied on, and adds what this
round needs:

build:  compile to a UNIQUE temporary output; only on success atomically rename to
        build/<target>.exe and write build/<target>.built.json with source hashes,
        the compile command, the executable hash and a build id. A failed compile
        leaves no runnable artifact behind.
run:    python tools_build.py run <target> [args...]  — refuses to run unless the
        build record exists and the executable hash matches it. ALSO refuses when any
        source recorded in the build has changed since: without that check, editing a
        source and forgetting to rebuild silently runs the OLD binary, which is exactly
        what happened once in E34 (the only symptom was a missing JSON field).
run_current:   same as `run`. The explicit name exists so new experiments can say which
        guarantee they are relying on.
run_recorded:  python tools_build.py run_recorded <target> [args...] — runs the binary
        the record describes WITHOUT re-verifying the sources, printing the drift and
        stating that the results do not describe the current source. For deliberately
        re-running a historical arm.

E25 additions:
  * V0/V1/V2 are ONE binary, three Config settings -- not three builds. The default
    (both flags off) reproduces E24 exactly, which is what lets the inherited gates
    pin V0 down.
  * check_e25_write: the differential gate. Three indexes from one plan, one
    operation sequence, compared ELEMENT BY ELEMENT after every batch. A missing
    comparison is not caught by a checksum, so it is not used.
  * the heavy counter build (E25_DIAG) and the clean timing build are separate
    targets. Algorithm-necessary maintenance must never live behind the diagnostic
    macro -- see the round's LOG.
  * no libart yet: the ART arm of the comparison is not built in this step.
  * the gates carry over unchanged.
"""
import hashlib, json, os, subprocess, sys, time

CXX = ["g++", "-std=c++17", "-O3", "-march=x86-64-v2", "-static"]
CC = ["gcc", "-std=c99", "-O3", "-march=x86-64-v2"]

# ITEM B: the sanitizer compiler line, used ONLY for the targets in SAN_TARGETS below.
# Two deliberate differences from CXX, and both matter:
#   * -static is DROPPED. The sanitizer runtime is a separate library that is not
#     compatible with a fully static link: the link fails, or the runtime never
#     initialises. This is the reason the sanitizer builds need their own line.
#   * -O1 -g instead of -O3. ASan's own instrumentation and allocator dominate the
#     runtime anyway, and -g is what makes a report name a source line. The goal of
#     these builds is COVERAGE, not speed -- their timings are meaningless and are
#     never reported as performance.
# The compiler itself is overridable, because a host can have a g++ with no
# sanitizer runtime at all (this machine's MinGW toolchain ships no libasan and no
# libubsan, so a sanitizer build there fails at link time):
#     set E25_SAN_CXX=clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
CXX_SAN = os.environ.get("E25_SAN_CXX", "").split() or [
    "g++", "-std=c++17", "-O1", "-g", "-march=x86-64-v2",
    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
]

# ITEM B: the targets built with CXX_SAN instead of CXX. Every one is a NEW target
# name over an EXISTING source (see SRC below), so no shipped target's flags, build
# record or executable is touched -- the same pattern already used for diag_long,
# which is bench_long.cpp under a second name. run_sanitizers.py runs exactly this set.
SAN_TARGETS = {
    "san_check_e31",
    "san_check_e9_multiblock",
    "san_check_e11_locate",
    "san_check_freeze",
    "san_bench_long_hyst",
    # E38: the LITS adapter is the first code in this tree that calls into a third-party
    # header with raw pointers and its own 1,024 allocations, so it is the first that most
    # needs the sanitizer rather than merely benefiting from it.
    "san_check_lits_model",
    "san_check_lits_fingerprint",
    "san_check_lits_index",
    # G1: the contract gate mutates -- it updates 4,096 values, inserts ~2,048 absent keys
    # through BOTH the primary and the overflow placement paths, and re-reads the group
    # geometry around every insert. That is exactly the kind of code ASan/UBSan is for, and
    # it is new code, so it is in the sanitizer set rather than merely benefiting from it.
    "san_check_g1_contract",
    # G3/A1': the widened reclaim is NEW code on the write path -- it repacks sub-blocks the
    # insert path never reaches, on a group whose placement has already failed once. That is
    # exactly where an offset or a boundary would be wrong if it were going to be.
    "san_check_widest",
    "san_check_compact_routing",
    "san_check_low_activity",
    # Candidate H (2026-10-11): the hole-placement path writes suffixes into dead ranges
    # and moves descriptors -- new code on the write side, so the sanitizer set first.
    "san_check_hole_reuse",
    # The mechanism diagnostic carries the same branch counters as the gate over a
    # longer workload, so it runs under the sanitizer too.
    "san_diag_low_activity",
    # Combination round: the gate mutates both mechanisms at once and asserts the
    # widened branch actually ran, so it runs under the sanitizer like its siblings.
    "san_check_combo",
    # Scan-entry round: the counter hand-check gate. Its FIRST registration named the
    # target but forgot this set, so the build used the normal compiler and two then
    # identical binaries (same exe hash) silently made the "sanitizer run" a no-op --
    # found by comparing the two build records. A gate is only as good as the compiler
    # line it was built with, so the entry lives here now.
    "san_check_scan_entry",
    # Scan-entry round: the S gate mutates all four arms and drives the ordered advance
    # through emptied sub-blocks, so it runs under the sanitizer like its siblings.
    "san_check_sround",
    # BlockLinearRank round: the gate fits the rank model over binary keys (empty, embedded
    # NUL, high bytes, 4,096-byte keys), compares EVERY corpus key against a re-derived
    # reference, then mutates an index and walks the per-region group vectors -- new code
    # over the encode / fit / map path, so it runs under the sanitizer like its siblings.
    "san_check_block_rank",
    "san_check_depth_dir",
    "san_check_depth_dir_trunc",
    # Byte-order gate (2026-10-11): it fits directories over NUL / 0x7f / 0x80 / 0xff keys
    # and drives a full index through scans and churn, so it runs under the sanitizer like
    # its siblings.
    "san_check_depth_dir_order",
}

# Per-target flags.
EXTRA = {
    "check_e11_locate": ["-DE12_COUNT"],   # this gate reads candidate_blocks
    "check_e19_reuse": ["-DE18_COUNT"],
    "check_e20_r2": ["-DE18_COUNT"],
    # The E25 gate asserts on the branch counters, so it is built WITH them. A gate
    # is not a timing build, so the counters cost nothing that is measured.
    "check_e25_write": ["-DE25_DIAG"],
    "diag_e25_calls": ["-DE25_DIAG"],      # the call matrix IS a counter build
    "diag_e26": ["-DE25_DIAG"],            # compaction diagnostics + tag-reuse check
    "diag_scan": ["-DE25_DIAG"],           # scan work breakdown
    "diag_long": ["-DE25_DIAG"],           # long-run structural counters
    "diag_transfer": ["-DE25_DIAG"],       # long-run: does a replacement change group?
    "diag_overflow": ["-DE25_DIAG"],       # long-run: why does an insert overflow?
    # The E31 gate asserts that the reclaim branch actually ran (subblock_reclaims > 0),
    # which needs the counters. A gate is not a timing build, so the counters cost
    # nothing that is measured. Source has an #error if this flag is missing.
    "check_e31": ["-DE25_DIAG"],
    # The scan-contract gate injects a deliberately NON-monotone group mapping through
    # the E25_DIAG-only hook on Region (Region::sar_rotate_groups), and asserts that the
    # injection really makes the scan misbehave -- with the hook off it also asserts that
    # a second index built the same way is identical to the first. The hook is not a
    # Config switch, so no switches.hpp table changes and the frozen configuration is
    # untouched. Source has an #error if this flag is missing. A gate is not a timing
    # build, so the counters cost nothing that is measured.
    # BlockLinearRank round: the gate reports the entry numbers (primary probes, full
    # comparisons, overflow entries). Those fields are written through E12_TICK, which is a
    # no-op in a shipped build, so the gate is built WITH the counters. A gate is not a
    # timing build, so the counters cost nothing that is measured.
    # BlockLinearRank round (2026-10-11): this gate carries an #error without it, so a build
    # that forgot the macro is a hard stop rather than a gate that silently skips its mapping
    # comparison and its negative control.
    # BlockLinearRank round: the gate reports the entry numbers (primary probes, full
    # comparisons, overflow entries). Those fields are written through E12_TICK, which is a
    # no-op in a shipped build, so the gate is built WITH the counters. A gate is not a
    # timing build, so the counters cost nothing that is measured.
    "check_block_rank": ["-DE12_COUNT"],
    "san_check_block_rank": ["-DE12_COUNT"],
    # EqualDepthDir gate: it reports the entry counters (primary probes, comparisons, overflow),
    # which are written through E12_TICK -- a no-op in a shipped build.
    "check_depth_dir": ["-DE12_COUNT"],
    "san_check_depth_dir": ["-DE12_COUNT"],
    # TRUNCDIR gate: same entry counters over the TRUNCATED directory, so the same macro rule.
    "check_depth_dir_trunc": ["-DE12_COUNT"],
    "san_check_depth_dir_trunc": ["-DE12_COUNT"],
    # P1 read-path entry diagnosis (2026-10-11): read-side counters only, no timing; the
    # device asserts its own mode and reconciliation, so it takes both diagnostic macros.
    "diag_depth_dir_read_path": ["-DE25_DIAG", "-DE12_COUNT"],
    "check_scan_contract": ["-DE25_DIAG"],
    # The O1 gate asserts that the stable-store arm actually used the new
    # representation (its record-shift counter stays 0 while its handle counter rises)
    # and that the erases really reached the overflow container. Both are read from the
    # E25 counters, so this target is built WITH them -- as check_e31 is. Source has an
    # #error if the flag is missing, so a wrong build is a hard stop rather than a report
    # full of zeros that looks like the container was never touched. A gate is not a
    # timing build, so the counters cost nothing that is measured.
    "check_overflow_store": ["-DE25_DIAG"],
    # The hysteresis counters are read by bench_long_hyst's own non-vacuous gate,
    # so the diagnostic build of the SAME source is a second target (as diag_long is
    # for bench_long). The clean timing build stays counter-free.
    "diag_hyst": ["-DE25_DIAG"],
    # The O1 arm asserts on its own movement counters, so the diagnostic build of the
    # same source is a second target, as diag_hyst is for bench_long_hyst.
    "diag_o1": ["-DE25_DIAG"],
    # The E35 prefix diagnostic: same source, diagnostic counters in.
    "diag_prefix": ["-DE25_DIAG"],
    # The E36 composition diagnostic needs the shift counters, so it is diagnostic-only.
    "diag_compose": ["-DE25_DIAG"],
    # E36: the gate reads the tag counters for its non-vacuity assertions, and the
    # benchmark has a diagnostic target for the same counters.
    "check_tag_store": ["-DE25_DIAG"], "diag_tag": ["-DE25_DIAG"],
    # E37: the migration feasibility scan is diagnostic-only by construction.
    "diag_migrate": ["-DE25_DIAG"],
    "check_migrate": ["-DE25_DIAG"],
    # E37: the R0/R1 benchmark's diagnostic twin. Named _b because diag_migrate is
    # already the feasibility program.
    "diag_migrate_b": ["-DE25_DIAG"],
    # E37 step 2b: repairing an already-degraded snapshot.
    "diag_repair": ["-DE25_DIAG"],
    "bench_w_t": ["-DE30_TIME"],           # same source as bench_w, clock ON
    # Needs BOTH: e30_probe is declared inside the E25_DIAG member block because it
    # calls diag_group(), so -DE30_TIME alone leaves it uncompiled.
    "bench_repack": ["-DE30_TIME", "-DE25_DIAG"],
    # E31 item 3, and it needs both for the same reason plus one more: the overflow
    # clocks are E30_T0/E30_ADD (so -DE30_TIME), and the movement counters and the
    # container-size accessor are declared under -DE25_DIAG. The source carries an
    # #error if either is missing, so a wrong build is a hard stop rather than a report
    # full of zeros that looks like "the overflow container is free".
    "diag_ov_cost": ["-DE30_TIME", "-DE25_DIAG"],
    # ITEM B: the sanitizer targets take the SAME flags as the shipped target built
    # from the same source, for the same reasons -- check_e31 asserts that the reclaim
    # branch ran (its source carries an #error without the counters) and
    # check_e11_locate reads candidate_blocks. A sanitizer build is not a timing
    # build either, so the counters cost nothing that is measured anywhere.
    "san_check_e11_locate": ["-DE12_COUNT"],
    "san_check_e31": ["-DE25_DIAG"],
    # E38: the index gate asserts that the prefix-rebuild, split and compaction branches RAN,
    # so it is built WITH the counters. A gate is not a timing build, so they cost nothing that
    # is measured -- and a gate that passes without executing the branch it names proves
    # nothing (E31's lesson, which is why the assertion is here rather than in a comment).
    "check_lits_index": ["-DE25_DIAG"],
    "san_check_lits_index": ["-DE25_DIAG"],
    # E38/P3: the diagnostic twin of the three-arm benchmark. One source, two targets --
    # 'diag_lits_adapter' carries the counters, 'bench_lits_adapter' the timings. The timing
    # build stays counter-free, because the counting cost sits inside the comparison loop and
    # is not symmetric between the arms.
    "diag_lits_adapter": ["-DE25_DIAG"],
    "diag_lits_adapter_m": ["-DE25_DIAG", "-DE30_TIME"],
    # G1: the contract gate reads the query-path group through Index::diag_where, which is
    # diagnostic-only, so it is built WITH the counters. A gate is not a timing build, so
    # they cost nothing that is measured. Source carries an #error without this flag, so a
    # wrong build is a hard stop rather than a report that silently skips part C.
    "check_g1_contract": ["-DE25_DIAG"],
    # G3/A1': the widened-reclaim gate asserts that the new branch actually ran
    # (reclaim_widened > 0), which needs the counters. Source carries an #error without this
    # flag. A gate is not a timing build, so the counters cost nothing that is measured.
    "check_widest": ["-DE25_DIAG"],
    "diag_lits2x2": ["-DE25_DIAG"],
    "check_i10_commit": ["-DE25_DIAG"],
    "diag_scan_nav": ["-DE25_DIAG"],
    "diag_a2_entry": ["-DE25_DIAG"],
    "diag_ov_cost_a3": ["-DE25_DIAG", "-DE30_TIME"],
    # Branch C: the capacity/decline probe reads the high-water and the drop/regrowth
    # trackers, which are compiled out of every timing build. No -DE30_TIME: this round
    # takes no timings at all -- counts locate candidates, they do not price them.
    "diag_branchc": ["-DE25_DIAG"],
    "diag_branchc2": ["-DE25_DIAG"],   # branch C2: the shrink-to-peak new-peak rate
    "diag_write_cost": ["-DE25_DIAG", "-DE18_COUNT", "-DE30_TIME", "-DE30_WRITE"],   # E3 entry: the write-path budget
    # Hole reuse: the event log is E25_DIAG-only, and this round takes no timings at all --
    # counts locate candidates, they do not price them. No -DE30_TIME.
    # -DE18_COUNT as well: E18's counters live behind their own guard, and the
    # insert-level overflow count is the denominator the per-block refusals have to
    # be read against. Counter-only build, still no timing macro of any kind.
    "diag_holes": ["-DE25_DIAG", "-DE18_COUNT"],
    # Compact region routing: the gate compares the routed and scanned searches key by key
    # through Index::diag_locate_*, which are diagnostic-only. A gate is not a timing build.
    "check_compact_routing": ["-DE25_DIAG"],
    # Low-activity reclaim gate: it asserts the branch counter is non-zero, which needs
    # the counters. A gate is not a timing build.
    "check_low_activity": ["-DE25_DIAG"],
    # Candidate H gate: asserts the branch counters are non-zero and reads the hole
    # counters beside the repack counters, which needs the counters. Not a timing build.
    "check_hole_reuse": ["-DE25_DIAG"],
    "san_check_hole_reuse": ["-DE25_DIAG"],
    # Low-activity MECHANISM diagnostic: counts only, no timing, and it reads the E18
    # insert-level overflow count beside the block-level refusals, so both guards are on.
    "diag_low_activity": ["-DE25_DIAG", "-DE18_COUNT"],
    "san_diag_low_activity": ["-DE25_DIAG", "-DE18_COUNT"],

    # The combination round (WIDE x compact routing). The gate asserts the widened branch
    # ran on ITS load, so it is built with the counters; the diagnostic twin of the timing
    # benchmark proves the same on the timing workload itself, where the clean build
    # compiles the counters out entirely.
    "check_combo": ["-DE25_DIAG"],
    "san_check_combo": ["-DE25_DIAG"],
    "diag_combo": ["-DE25_DIAG"],
    # Scan-entry round stage B2: ONE source, two targets (the bench_w/bench_w_t pattern).
    # This target runs the materialised and the no-materialisation scan side by side for
    # the W/B discriminator; -DE25_SCAN_CONSUME is defined by NO other target, so the
    # shipped bench_combo binary cannot see the consume path at all.
    "bench_scanconsume": ["-DE25_SCAN_CONSUME"],
    # Scan-entry round: the per-call first-batch probe. A copy of bench_combo.cpp; no
    # extra flags -- it is a timing build and carries no counters.
    "bench_firstscan": [],
    # The S-armed twin of the first-batch probe: same source, the S round's arm set.
    "bench_firstscan_s": ["-DE25_ARMS_S"],
    # Scan-entry round, candidate S: the four-arm comparison B / W / S / W+S. One source,
    # two targets (the bench_w/bench_w_t pattern); -DE25_ARMS_S is defined by NO other
    # target, so the shipped bench_combo arm set cannot move underneath itself.
    "bench_sround": ["-DE25_ARMS_S"],
    # Candidate H A/B (2026-10-11): same combo framework, W / WH / W / WH arm slots.
    "bench_combo_h": ["-DE25_ARMS_H"],
    # Scan-entry round: the S gate asserts that the ordered advance ran and saved
    # comparisons, so it is built WITH the counters; its sanitizer twin takes the same
    # flags (and is registered in SAN_TARGETS below).
    "check_sround": ["-DE25_DIAG", "-DE25_ARMS_S"],
    # Phase D entry diagnosis (2026-10-10, NEXT_ACTION_PLAN_20261010_D.md). ONE source, TWO
    # targets, and the split IS the requirement: the count build carries no clock and the
    # phase-time build carries no counter, so no field in a row can be a zero that reads as
    # "this work does not happen". The source carries an #error if both groups are defined or
    # neither is, and `build` in every row names which one produced it.
    "diag_write_residual": ["-DE25_DIAG", "-DE18_COUNT"],
    "diag_write_residual_t": ["-DE30_TIME", "-DE30_WRITE"],
    # Stage D probe: the timing build with the write path's SEARCH using the boundary
    # directory (write_locate). Prices the per-block loop before a candidate is defined.
    "diag_write_residual_tl": ["-DE30_TIME", "-DE30_WRITE", "-DDR_WRITE_LOCATE"],
    # The sanitizer twin takes the SAME flags as the shipped target built from the same
    # source (the source carries an #error without -DE25_DIAG, which is exactly how the
    # missing entry announced itself on the first attempt).
    "san_check_sround": ["-DE25_DIAG", "-DE25_ARMS_S"],
    # Stage A scan-entry round: the counter hand-check reads the per-call scan counters and
    # the diagnostic accessors, so it is built WITH the counters. A gate is not a timing
    # build -- nothing here is ever timed.
    "check_scan_entry": ["-DE25_DIAG"],
    # One source, two targets -- the sanitizer build cannot drift from the gate it shadows.
    "san_check_scan_entry": ["-DE25_DIAG"],
    # Compact region routing A/B. NO diagnostic flags: it is a timing build, and the
    # counters would both slow it and make its identity different from the arm it times.
    "bench_routing": [],
    "san_check_i10_commit": ["-DE25_DIAG"],
    # The vendored B+ tree lives outside the round root, so its include path is
    # spelled out here. -I is relative to the round directory (the cwd of the
    # compile), which keeps the recorded command reproducible from the repo root.
    "bench_btree": ["-Ithird_party/vendor/tlx"],
    "diag_btree": ["-DE25_DIAG", "-Ithird_party/vendor/tlx"],
    # The sanitizer build of the SAME source takes the SAME flags, for the same reason --
    # without -DE25_DIAG the source refuses to compile at all (it carries an #error), so a
    # wrong build is a hard stop rather than a run that silently skips part C.
    "san_check_g1_contract": ["-DE25_DIAG"],
    "san_check_widest": ["-DE25_DIAG"],
    "san_check_compact_routing": ["-DE25_DIAG"],
    "san_check_low_activity": ["-DE25_DIAG"],
    # G4/E1: PGO/LTO. FOUR build variants of ONE source, so the algorithm, the ISA target and
    # the data structures are identical and only the compilation differs.
    #
    #   bench_lto      LTO only
    #   bench_pgo      PGO only            (profile read from build/pgo_profile)
    #   bench_pgo_lto  both
    #   bench_pgo_gen  the INSTRUMENTED build that produces the profile -- never timed
    #
    # -fprofile-correction is here because bench_widest runs several independent processes and
    # g++ merges their profiles; without it g++ discards counters it considers inconsistent.
    # It changes which counters are used, not the source or the semantics.
    "bench_lto": ["-flto"],
    "bench_pgo": ["-fprofile-use=build/pgo_profile", "-fprofile-correction", "-Wmissing-profile"],
    "bench_pgo_lto": ["-flto", "-fprofile-use=build/pgo_profile", "-fprofile-correction",
                      "-Wmissing-profile"],
    "bench_pgo_gen": ["-fprofile-generate=build/pgo_profile"],
}

# PGO NEEDS A STABLE OBJECT NAME, and that is the one thing the unique temporary below cannot
# give it. g++ derives the .gcda file name from the OBJECT's output path, so a build that
# compiles to `<target>.<pid>.<ns>.tmp.exe` looks for a file name that no previous build could
# have produced -- and a naive two-step PGO then produces a perfectly good binary with NO
# profile applied. That happened here: the build succeeded, and only -Wmissing-profile said so.
#
# These three targets therefore share ONE fixed object path, so the generator's profile is
# found by both consumers. `bench_lto` is NOT in this table: it has no profile to find, and it
# keeps the unique temporary.
#
# ATOMICITY IS UNCHANGED. The binary still appears only on success, via the same os.replace.
# What changes is that two CONCURRENT builds of the same target would now collide instead of
# coexisting. Builds in this project are serial and the PGO pair is driven by one script.
STABLE_TMP = {
    "bench_pgo_gen": "pgo_obj.tmp.exe",
    "bench_pgo": "pgo_obj.tmp.exe",
    "bench_pgo_lto": "pgo_obj.tmp.exe",
}
LIBS = {"bench_e25": ["build/art.o"], "bench_e26": ["build/art.o"],
        "bench_long_art": ["build/art.o"], "bench_widest": ["build/art.o"],
        "bench_lits2x2": ["build/art.o"],
        "bench_i10": ["build/art.o"],
        "bench_btree": ["build/art.o"],
        "diag_btree": ["build/art.o"],
        "check_i10_commit": ["build/art.o"],
        "diag_scan_nav": ["build/art.o"],
        "diag_a2_entry": ["build/art.o"],
        "diag_ov_cost_a3": ["build/art.o"],
        "diag_branchc": ["build/art.o"],
        "diag_branchc2": ["build/art.o"],
        "diag_write_cost": ["build/art.o"],
        "diag_holes": ["build/art.o"],
        "check_compact_routing": ["build/art.o"],
        "check_low_activity": ["build/art.o"],
        "san_check_compact_routing": ["build/art.o"],
        "san_check_low_activity": ["build/art.o"],
        "check_hole_reuse": ["build/art.o"],
        "san_check_hole_reuse": ["build/art.o"],
        "diag_low_activity": ["build/art.o"],
        "san_diag_low_activity": ["build/art.o"],
        "bench_routing": ["build/art.o"],
        "san_check_i10_commit": ["build/art.o"],
        "diag_lits2x2": ["build/art.o"],
        "bench_lto": ["build/art.o"], "bench_pgo": ["build/art.o"],
        "bench_pgo_lto": ["build/art.o"], "bench_pgo_gen": ["build/art.o"], "bench_long_hyst": ["build/art.o"],
        "diag_hyst": ["build/art.o"], "san_bench_long_hyst": ["build/art.o"],
        "bench_long_o1": ["build/art.o"], "diag_o1": ["build/art.o"],
        "bench_prefix": ["build/art.o"], "diag_prefix": ["build/art.o"],
        "diag_compose": ["build/art.o"],
        "bench_tag": ["build/art.o"], "diag_tag": ["build/art.o"],
        "diag_migrate": ["build/art.o"],
        "bench_migrate": ["build/art.o"], "diag_migrate_b": ["build/art.o"],
        "diag_repair": ["build/art.o"],
        "bench_lits_adapter": ["build/art.o"],
        "diag_lits_adapter": ["build/art.o"],
        "diag_lits_adapter_m": ["build/art.o"]}
C_SOURCES = {"bench_e25": ["third_party/vendor/libart/src/art.c"],
             "bench_e26": ["third_party/vendor/libart/src/art.c"],
             "bench_long_art": ["third_party/vendor/libart/src/art.c"],
             "bench_long_hyst": ["third_party/vendor/libart/src/art.c"],
             "diag_hyst": ["third_party/vendor/libart/src/art.c"],
             "san_bench_long_hyst": ["third_party/vendor/libart/src/art.c"],
             "bench_long_o1": ["third_party/vendor/libart/src/art.c"],
             "diag_o1": ["third_party/vendor/libart/src/art.c"],
             "bench_prefix": ["third_party/vendor/libart/src/art.c"],
             "diag_prefix": ["third_party/vendor/libart/src/art.c"],
             "diag_compose": ["third_party/vendor/libart/src/art.c"],
             "bench_tag": ["third_party/vendor/libart/src/art.c"],
             "diag_tag": ["third_party/vendor/libart/src/art.c"],
             "diag_migrate": ["third_party/vendor/libart/src/art.c"],
             "bench_migrate": ["third_party/vendor/libart/src/art.c"],
             "diag_migrate_b": ["third_party/vendor/libart/src/art.c"],
             "diag_repair": ["third_party/vendor/libart/src/art.c"],
             "bench_lits_adapter": ["third_party/vendor/libart/src/art.c"],
             "diag_lits_adapter": ["third_party/vendor/libart/src/art.c"],
             "diag_lits_adapter_m": ["third_party/vendor/libart/src/art.c"],
             # Candidate H (2026-10-11): these two live here rather than only in LIBS so the
             # art object is rebuilt by the INVOKING toolchain on every build -- the stale
             # MinGW/COFF art.o left in build/ made the first sanitizer link fail with PE
             # relocations (R_AMD64_IMAGEBASE), and a target that compiles its own C source
             # cannot inherit a foreign-format object from an earlier episode.
             "check_hole_reuse": ["third_party/vendor/libart/src/art.c"],
             "san_check_hole_reuse": ["third_party/vendor/libart/src/art.c"]}
# diag_long is the SAME source as bench_long, built with the counters in. One source,
# two targets, so the two builds cannot drift apart.
SRC = {"diag_long": "bench_long.cpp", "bench_w_t": "bench_w.cpp",
       "diag_hyst": "bench_long_hyst.cpp", "diag_o1": "bench_long_o1.cpp",
       "bench_prefix": "bench_prefix.cpp", "diag_prefix": "bench_prefix.cpp",
       "diag_compose": "diag_compose.cpp", "diag_tag": "bench_tag.cpp",
       "diag_migrate": "diag_migrate.cpp", "diag_migrate_b": "bench_migrate.cpp",
       "diag_repair": "diag_repair.cpp",
       "diag_lits_adapter": "bench_lits_adapter.cpp",
       "diag_lits_adapter_m": "bench_lits_adapter.cpp",
       # ITEM B: the sanitizer targets are new NAMES over these same sources. One
       # source, two targets -- as diag_long is for bench_long -- so the sanitizer
       # build and the shipped build of the same program cannot drift apart.
       "san_check_e31": "check_e31.cpp",
       "san_check_e9_multiblock": "check_e9_multiblock.cpp",
       "san_check_e11_locate": "check_e11_locate.cpp",
       "san_check_freeze": "check_freeze.cpp",
       "san_bench_long_hyst": "bench_long_hyst.cpp",
       "san_check_lits_model": "check_lits_model.cpp",
       "san_check_lits_fingerprint": "check_lits_fingerprint.cpp",
       "san_check_lits_index": "check_lits_index.cpp",
       # G1: one source, two targets -- the sanitizer build and the shipped build of the
       # same gate cannot drift apart.
       "san_check_g1_contract": "check_g1_contract.cpp",
       # G3/A1': one source, two targets.
       "san_check_widest": "check_widest.cpp",
       "san_check_compact_routing": "check_compact_routing.cpp",
       "san_check_low_activity": "check_low_activity.cpp",
       # Candidate H: one source, two targets -- the gate and its sanitizer twin.
       "san_check_hole_reuse": "check_hole_reuse.cpp",
       "san_diag_low_activity": "diag_low_activity.cpp",
       # Combination round: one source, two targets -- the gate and its sanitizer twin.
       "san_check_combo": "check_combo.cpp",
       "diag_combo": "bench_combo.cpp",
       "bench_scanconsume": "bench_combo.cpp",
       "bench_firstscan": "bench_firstscan.cpp",
       "bench_firstscan_s": "bench_firstscan.cpp",
       "bench_sround": "bench_combo.cpp",
       "bench_combo_h": "bench_combo.cpp",
       "check_sround": "check_sround.cpp",
       "san_check_sround": "check_sround.cpp",
       # BlockLinearRank round (2026-10-11): one source, two targets -- the gate and its
       # sanitizer twin, so the two builds of the same gate cannot drift apart.
       "check_block_rank": "check_block_rank.cpp",
       "san_check_block_rank": "check_block_rank.cpp",
       # Equal-depth threshold directory: the entry diagnosis. One source, no counters (it reads
       # Region::groups()/group_stats() and does its own arithmetic), no timing.
       "diag_threshold_entry": "diag_threshold_entry.cpp",
       # EqualDepthDir: one source, two targets (gate + sanitizer twin).
       "check_depth_dir": "check_depth_dir.cpp",
       "san_check_depth_dir": "check_depth_dir.cpp",
       # TRUNCDIR: one source, two targets -- the gate and its sanitizer twin.
       "check_depth_dir_trunc": "check_depth_dir_trunc.cpp",
       "san_check_depth_dir_trunc": "check_depth_dir_trunc.cpp",
       # P1 read-path entry diagnosis: one source, no sanitizer twin yet (counts-only; the
       # device fails closed on its own assertions).
       "diag_depth_dir_read_path": "diag_depth_dir_read_path.cpp",
       # Byte-order gate: one source, two targets -- the gate and its sanitizer twin. It needs
       # no diagnostic counters: every count it uses comes from its own reference.
       "check_depth_dir_order": "check_depth_dir_order.cpp",
       "san_check_depth_dir_order": "check_depth_dir_order.cpp",
       # EqualDepthDir A/B: two arms, position rotated, paired within a repetition.
       "bench_depth_dir": "bench_depth_dir.cpp",
       # Phase D entry diagnosis: one source, two builds (see EXTRA).
       "diag_write_residual": "diag_write_residual.cpp",
       "diag_write_residual_t": "diag_write_residual.cpp",
       "diag_write_residual_tl": "diag_write_residual.cpp",
       # Stage A: the scan-entry counter hand-check. One source, two targets -- the gate and
       # its sanitizer twin.
       "check_scan_entry": "check_scan_entry.cpp",
       "san_check_scan_entry": "check_scan_entry.cpp",
       # G4/E1: one source, four builds. Same source as bench_widest, so the four variants
       # cannot drift apart from each other or from the timing baseline.
       "bench_lto": "bench_widest.cpp",
       "bench_pgo": "bench_widest.cpp",
       "bench_pgo_lto": "bench_widest.cpp",
       "bench_pgo_gen": "bench_widest.cpp",
       # G5: the 2x2 cross-backend row. Same benchmark, four Index arms
       # {S0, WIDE} x {Context16, LitsHpt}, plus libart.
       "bench_lits2x2": "bench_lits2x2.cpp",
       "diag_btree": "bench_btree.cpp",
       # I10: old vs safe compaction path, paired. Same source as bench_long_hyst.cpp
       # with the arm set replaced; that file and its record are left untouched.
       "bench_i10": "bench_i10.cpp",
       # I10: the commit-phase atomicity gate. Diagnostic-only, so it is never timing.
       "check_i10_commit": "check_i10_commit.cpp",
       "san_check_i10_commit": "check_i10_commit.cpp",
       # G5 external baseline: TLX B+ tree, vendored under third_party/vendor/tlx.
       "bench_btree": "bench_btree.cpp",
       "diag_lits2x2": "bench_lits2x2.cpp"}

DIR = os.path.dirname(os.path.abspath(__file__))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def archive_stale_record(target, rec):
    """Before replacing logs/build_<target>.json, preserve the record being replaced.

    That path is FIXED and both writers in build() open it with "w", so building a target a
    second time used to overwrite the previous identity -- build_id, executable hash, source
    hashes -- in place. A report that quotes those fields then points at numbers that exist
    nowhere afterwards. This round did exactly that to fourteen gates while re-establishing
    their evidence against the current source, which is how the hole was noticed: the
    freeze report's quoted check_e8_hetero build_id survives only as text in that report.

    Keyed by build_id, so building the same target repeatedly keeps EVERY identity rather
    than only the last, and archiving an already-archived record is a no-op. Nothing is
    deleted, and a target whose record has not changed is not touched at all.
    """
    path = os.path.join(DIR, "logs", f"build_{target}.json")

    if not os.path.exists(path):
        return

    try:
        with open(path, encoding="utf-8") as f:
            old = json.load(f)
    except Exception:
        return

    old_id = old.get("build_id")

    if not old_id or old_id == rec.get("build_id"):
        return

    adir = os.path.join(DIR, "logs", "archive")
    os.makedirs(adir, exist_ok=True)
    dest = os.path.join(adir, f"build_{target}.{old_id}.json")

    if os.path.exists(dest):
        return

    with open(dest, "w", encoding="utf-8") as f:
        json.dump(old, f, ensure_ascii=False, indent=1)


def compile_c(target):
    """Compile the C dependencies of a target into build/<name>.o."""
    objs = []
    for rel in C_SOURCES.get(target, []):
        obj = os.path.join(DIR, "build", os.path.splitext(os.path.basename(rel))[0] + ".o")
        cmd = CC + ["-c", os.path.join(DIR, rel), "-o", obj]
        r = subprocess.run(cmd, cwd=DIR, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"C BUILD FAILED: {rel}\n{r.stderr[-2000:]}", file=sys.stderr)
            sys.exit(1)
        objs.append(obj)
    return objs


def build(target):
    src = os.path.join(DIR, SRC.get(target, target + ".cpp"))
    exe = os.path.join(DIR, "build", target + ".exe")
    # See STABLE_TMP: the PGO pair must compile to a fixed object path or the profile it
    # writes is looked for under a name that never existed.
    tmp_name = STABLE_TMP.get(target, f"{target}.{os.getpid()}.{time.time_ns()}.tmp.exe")
    tmp = os.path.join(DIR, "build", tmp_name)

    objs = compile_c(target)
    link = [os.path.join(DIR, p) for p in LIBS.get(target, [])]

    # ITEM B: a sanitizer target takes CXX_SAN instead of CXX (no -static; see the
    # constant at the top). This is the ONLY change to this function. Everything
    # else on this line and around it is unchanged: the unique temporary output, the
    # atomic rename on success, the source hashes, the executable hash, and the
    # removal of the stale artifact after a failed compile.
    cxx = CXX_SAN if target in SAN_TARGETS else CXX
    cmd = cxx + EXTRA.get(target, []) + [src] + link + ["-o", tmp]
    rec = {"target": target, "build_id": f"{time.time_ns()}", "command": " ".join(cmd), "sources": {}}
    rec["sources"][os.path.basename(src)] = sha256(src)
    # Headers are tracked RECURSIVELY. This loop used to be os.listdir(DIR), which sees only
    # the round's ROOT -- so a header under model/ or vendor/ was invisible to the strict run
    # entry and an edit to it would not have refused the run. That is the same shape of gap
    # as the E34 accident this entry exists to prevent, and E38 is the first round to put
    # headers in subdirectories. Paths are recorded with forward slashes, which
    # source_drift() already resolves; root-level headers keep their bare names, so every
    # existing record still resolves exactly as before.
    #
    # vendor/lits_model_original/ is EXCLUDED on purpose: it is the provenance archive, not
    # an input to any binary. Folding it in would make a change to the archived upstream
    # bytes refuse a run for a build-input reason, which is the wrong diagnosis for the
    # right suspicion -- the archive's own integrity is checked by fetch_lits_pinned.py
    # against E4's recorded hashes, and by patch_lits_safety.py against E4's patched hash.
    for root, dirs, files in os.walk(DIR):
        dirs[:] = sorted(d for d in dirs
                         if d not in ("build", "logs", "__pycache__", "lits_model_original"))
        for f in sorted(files):
            if not f.endswith(".hpp"):
                continue
            full = os.path.join(root, f)
            rec["sources"][os.path.relpath(full, DIR).replace(os.sep, "/")] = sha256(full)
    for rel in C_SOURCES.get(target, []):
        rec["sources"][rel] = sha256(os.path.join(DIR, rel))
    rec["extra_flags"] = EXTRA.get(target, [])
    rec["c_objects"] = [os.path.relpath(o, DIR).replace(os.sep, "/") for o in objs]

    r = subprocess.run(cmd, cwd=DIR, capture_output=True, text=True)
    rec["returncode"] = r.returncode
    rec["stderr_tail"] = r.stderr[-4000:]

    if r.returncode != 0 or not os.path.exists(tmp):
        rec["status"] = "COMPILE_FAILED"
        rec["exe_sha256"] = None
        archive_stale_record(target, rec)
        with open(os.path.join(DIR, "logs", f"build_{target}.json"), "w", encoding="utf-8") as f:
            json.dump(rec, f, ensure_ascii=False, indent=1)
        if os.path.exists(tmp):
            os.remove(tmp)
        # A failed compile must not leave the PREVIOUS artifact runnable. `run` reads
        # build/<target>.built.json, and that file used to survive a failure -- so a
        # gate whose source no longer compiled still executed its last good binary and
        # reported a pass. That happened once in this round. Remove both, so a failure
        # is a hard stop rather than a silent fallback to a stale build.
        for stale in (os.path.join(DIR, "build", f"{target}.built.json"),
                      os.path.join(DIR, "build", target + ".exe")):
            if os.path.exists(stale):
                os.remove(stale)
        print(f"BUILD FAILED: {target} — no runnable artifact produced "
              f"(stale build record and executable removed)", file=sys.stderr)
        sys.exit(1)

    os.replace(tmp, exe)          # atomic: the artifact exists only on success
    rec["exe_sha256"] = sha256(exe)
    rec["status"] = "OK"
    archive_stale_record(target, rec)
    with open(os.path.join(DIR, "logs", f"build_{target}.json"), "w", encoding="utf-8") as f:
        json.dump(rec, f, ensure_ascii=False, indent=1)
    with open(os.path.join(DIR, "build", f"{target}.built.json"), "w", encoding="utf-8") as f:
        json.dump(rec, f, ensure_ascii=False, indent=1)
    print(f"OK {target} build_id={rec['build_id']} exe={rec['exe_sha256'][:16]}…")


def load_record(target):
    """The build record, or a refusal. Shared by every run entry."""
    built = os.path.join(DIR, "build", f"{target}.built.json")
    if not os.path.exists(built):
        print(f"RUN REFUSED: no successful build record for {target} (run tools_build.py first)", file=sys.stderr)
        sys.exit(2)

    rec = json.load(open(built, encoding="utf-8"))
    exe = os.path.join(DIR, "build", target + ".exe")

    if rec.get("status") != "OK" or not os.path.exists(exe) or sha256(exe) != rec.get("exe_sha256"):
        print(f"RUN REFUSED: executable hash does not match build record for {target} "
              f"(stale or tampered binary)", file=sys.stderr)
        sys.exit(2)

    return rec, exe


def source_drift(rec):
    """Every source in the record whose current content differs.

    Returns a list of (name, status) where status is 'changed' or 'missing'. An empty
    list means the binary in build/ was produced from the sources now on disk.
    """
    drifted = []

    for name, recorded in (rec.get("sources") or {}).items():
        full = os.path.join(DIR, name.replace("/", os.sep))

        if not os.path.exists(full):
            full = os.path.join(DIR, os.path.basename(name))

        if not os.path.exists(full):
            drifted.append((name, "missing"))
        elif sha256(full) != recorded:
            drifted.append((name, "changed"))

    return drifted


def report_drift(target, rec, drifted, pad):
    """One block, used by the strict refusal and the recorded-run banner."""
    print("%sbuild_id %s was made from %d source(s); %d differ from the ones on disk now:"
          % (pad, rec.get("build_id"), len(rec.get("sources") or {}), len(drifted)), file=sys.stderr)

    for name, status in drifted[:20]:
        print("%s  %-10s %s" % (pad, status, name), file=sys.stderr)

    if len(drifted) > 20:
        print("%s  ... and %d more" % (pad, len(drifted) - 20), file=sys.stderr)


def run_current(target, args):
    """The default entry: the binary must match BOTH its executable hash and its sources."""
    rec, exe = load_record(target)
    drifted = source_drift(rec)

    if drifted:
        print(f"RUN REFUSED: {target} was built from sources that have since changed "
              f"(refusing to run a binary that does not match the tree)", file=sys.stderr)
        report_drift(target, rec, drifted, "  ")
        print("  Rebuild the target (python3 tools_build.py %s), or use run_recorded to "
              "re-run the recorded binary deliberately." % target, file=sys.stderr)
        sys.exit(2)

    sys.exit(subprocess.run([exe] + args, cwd=DIR).returncode)


def run_recorded(target, args):
    """Run the recorded binary even if the sources moved on, and say so."""
    rec, exe = load_record(target)
    drifted = source_drift(rec)

    print("RUN RECORDED: %s, build_id %s" % (target, rec.get("build_id")), file=sys.stderr)

    if drifted:
        print("  WARNING: the sources have changed since this binary was built, so these "
              "results DESCRIBE THE RECORDED BUILD, NOT THE CURRENT SOURCE.", file=sys.stderr)
        report_drift(target, rec, drifted, "  ")
    else:
        print("  (sources are unchanged: this binary does match the current tree)", file=sys.stderr)

    sys.exit(subprocess.run([exe] + args, cwd=DIR).returncode)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    if sys.argv[1] == "run_current":
        run_current(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == "run_recorded":
        run_recorded(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == "run":
        # `run` is the strict entry. Fail-closed by default: the E34 accident (edit a
        # source, forget to rebuild, silently run the old binary) must not be reachable
        # by omission. Historical re-runs go through run_recorded, which says what it is.
        run_current(sys.argv[2], sys.argv[3:])
    else:
        for t in sys.argv[1:]:
            build(t)

#!/usr/bin/env python3
"""Repack split patch (2026-10-11).

Applies, with assertion-checked anchors, the ONE bounded instrumentation that
NEXT_ACTION_PLAN_20261011 asks for before any hole-reuse candidate is implemented:

  * index.hpp               -- rp_* work counters + w_rp_sort / w_rp_move in repack_subblock
  * diag_write_residual.cpp -- the two field lists and the rp_split header marker
  * analyze_write_residual.py -- strict split field checks, arithmetic guards, split tables
  * run_repack_split.sh     -- the round driver, written here so it keeps LF endings

Every substitution asserts exactly ONE occurrence of its original bytes, and the three
edited files are pinned to their stage-D pre-hashes, so the patch refuses a tree it was
not written for. Pre-hashes: index.hpp 6aa6be5c..., diag 51e1e8e1..., analyzer b5dd8bb1...
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


INDEX_PRE = "6aa6be5c8fd5d7a4c7fe9587b81308969f1d6ac7e26fbc24d46eba65365d0a49"
DIAG_PRE = "51e1e8e135ec24f26652f4a5888139b9dca5d61f97ff83eb32eff446162b0525"
ANALYZER_PRE = "b5dd8bb1098d509fa6da79ea2dc09cde7187abb99fcb02fb507524e9d3e54028"

INDEX_RP_FIELDS = """    // ---- Repack split (2026-10-11): the work side of one repack_subblock pass -----
    // The entry question for on-demand hole placement (candidate H) is what a reclaim
    // pass spends its time on. The E30 clocks carry the time side of the same split;
    // these count the WORK, exactly and machine-independently: a full pass processes n
    // records, issues one memmove per record and rewrites every offset. `moved` vs
    // `selfcopy` labels whether the record's offset actually changed -- a self-copy is
    // a same-address memmove, a call whose data movement does not exist. Function work,
    // not memory-bus traffic.
    std::uint64_t rp_calls = 0;            // full passes (sort + move reached)
    std::uint64_t rp_records = 0;          // records the packing loop handled
    std::uint64_t rp_bytes = 0;            // their suffix bytes (the memmove lengths)
    std::uint64_t rp_moved_records = 0;    // offset CHANGED: a real relocation
    std::uint64_t rp_moved_bytes = 0;
    std::uint64_t rp_selfcopy_records = 0;// offset unchanged: memmove(base+x, base+x, n)
    std::uint64_t rp_selfcopy_bytes = 0;
"""

INDEX_T_FIELDS = """    // Repack split (2026-10-11): the two segments of one FULL pass (n >= 2). sort =
    // order[] init + insertion sort; move = the packing memmoves + offset rewrites +
    // bottom. Each full pass reads one SHARED boundary between them and one at the end;
    // the n<2 early return stays in the parent alone. See repack_subblock for the shape.
    std::uint64_t w_rp_sort_ns = 0, w_rp_sort_calls = 0;     // sort end - entry
    std::uint64_t w_rp_move_ns = 0, w_rp_move_calls = 0;     // move end - sort end
"""

INDEX_PROLOGUE = """        // Repack split (2026-10-11): one reading at entry, ONE SHARED boundary after the
        // sort (sort's end == move's start) and one at the very end. The accumulation
        // happens after the last reading, so the write-back is outside the window, and no
        // second RAII pair is nested inside the parent clock. The parent keeps its
        // stage-D meaning: every call in w_reclaim_calls, and the n<2 early return is
        // charged to the parent alone, closed at the return itself.
#if defined(E30_TIME) && defined(E30_WRITE)
        const std::uint64_t rp_t0 = E30_T0();
#endif
"""

INDEX_EARLY = """            if (!config_.reclaim_low_activity) {
#if defined(E30_TIME) && defined(E30_WRITE)
                E30_ADD(w_reclaim_ns, rp_t0);
                E30_INC(w_reclaim_calls);
#endif
                return;
            }
"""

INDEX_MOVE = """#if defined(E30_TIME) && defined(E30_WRITE)
        const std::uint64_t rp_t1 = E30_T0();   // sort end == move start (shared)
#endif
        std::size_t top = bb_[g];
        for (std::size_t a = 0; a < n; ++a) {
            Entry& e = es[order[a]];
            top -= e.length;
            std::memmove(base + top, base + e.offset, e.length);
            // Work counters (count build only; the macros evaluate their argument only
            // there, so no comparison lands in the timed loop). The offset is read BEFORE
            // the rewrite: an unchanged offset is a same-address memmove -- counted, and
            // labelled self-copy rather than movement.
            E25_ADD(rp_records, 1);
            E25_ADD(rp_bytes, e.length);
            E25_ADD(rp_moved_records, (e.offset != top) ? 1 : 0);
            E25_ADD(rp_moved_bytes, (e.offset != top) ? e.length : 0);
            E25_ADD(rp_selfcopy_records, (e.offset == top) ? 1 : 0);
            E25_ADD(rp_selfcopy_bytes, (e.offset == top) ? e.length : 0);
            e.offset = static_cast<std::uint32_t>(top);
        }
        h.bottom = static_cast<std::uint16_t>(top);
#if defined(E30_TIME) && defined(E30_WRITE)
        const std::uint64_t rp_t2 = E30_T0();   // move end == whole pass end
        auto& rpT = ::validation::e30::timers();   // write-back, after the window
        rpT.w_reclaim_ns += rp_t2 - rp_t0;
        ++rpT.w_reclaim_calls;
        rpT.w_rp_sort_ns += rp_t1 - rp_t0;
        ++rpT.w_rp_sort_calls;
        rpT.w_rp_move_ns += rp_t2 - rp_t1;
        ++rpT.w_rp_move_calls;
#endif
"""

INDEX_SUBS = [
    ("    std::uint64_t fail_bytes_repack_guarded = 0;\n"
     "    std::uint64_t prefix_rebuilds = 0;",
     "    std::uint64_t fail_bytes_repack_guarded = 0;\n" + INDEX_RP_FIELDS +
     "    std::uint64_t prefix_rebuilds = 0;"),
    ("    std::uint64_t w_reclaim_ns = 0, w_reclaim_calls = 0;     // repack_subblock\n",
     "    std::uint64_t w_reclaim_ns = 0, w_reclaim_calls = 0;     // repack_subblock,"
     " ALL calls\n" + INDEX_T_FIELDS),
    ("        E30_W(reclaim);\n", INDEX_PROLOGUE),
    ("            if (!config_.reclaim_low_activity) return;\n", INDEX_EARLY),
    ("        auto* es = entries(g, blk);\n        std::byte* base = base_of(g, blk);\n",
     "        E25_INC(rp_calls);\n        auto* es = entries(g, blk);\n"
     "        std::byte* base = base_of(g, blk);\n"),
    ("        std::size_t top = bb_[g];\n"
     "        for (std::size_t a = 0; a < n; ++a) {\n"
     "            Entry& e = es[order[a]];\n"
     "            top -= e.length;\n"
     "            std::memmove(base + top, base + e.offset, e.length);\n"
     "            e.offset = static_cast<std::uint32_t>(top);\n"
     "        }\n"
     "        h.bottom = static_cast<std::uint16_t>(top);\n",
     INDEX_MOVE),
]

DIAG_SUBS = [
    ("    F(fail_bytes_repack_guarded) F(prefix_rebuilds) F(threshold_splits) ",
     "    F(fail_bytes_repack_guarded) \\\n"
     "    F(rp_calls) F(rp_records) F(rp_bytes) F(rp_moved_records) F(rp_moved_bytes) \\\n"
     "    F(rp_selfcopy_records) F(rp_selfcopy_bytes) \\\n"
     "    F(prefix_rebuilds) F(threshold_splits) "),
    ("    F(w_rebuild_all_records) F(w_reclaim_ns) F(w_reclaim_calls) ",
     "    F(w_rebuild_all_records) F(w_reclaim_ns) F(w_reclaim_calls) \\\n"
     "    F(w_rp_sort_ns) F(w_rp_sort_calls) F(w_rp_move_ns) F(w_rp_move_calls) "),
    (r'\"key_hash\":" << key_hash', r'\"rp_split\":1,\"key_hash\":" << key_hash'),
]

RP_CONSTANTS = '''
# Repack split (2026-10-11). Present only in files whose header carries "rp_split": 1;
# check_file adds them to the required set for exactly those files, so the stage-D files
# (no marker) still analyze with their own field set. The count side and the time side
# are separate lists because the two builds are mutually exclusive by construction.
RP_COUNT_FIELDS = """rp_calls rp_records rp_bytes rp_moved_records rp_moved_bytes
rp_selfcopy_records rp_selfcopy_bytes""".split()
RP_TIME_FIELDS = "w_rp_sort_ns w_rp_sort_calls w_rp_move_ns w_rp_move_calls".split()
'''

RP_CHECK_FILE = '''def check_file(path, rows, require_split=False):
    hdr = rows[0]
    if hdr.get("t") != "hdr":
        fail(path + ": first row is not the header")
    for k in HDR_BASE:
        if k not in hdr:
            fail(path + ": header field missing: " + k)
    build = hdr["build"]
    rp_split = hdr.get("rp_split") == 1
    if require_split and not rp_split:
        fail(path + ": --require-rp-split was given but the header carries no rp_split marker")
    expected = (E25_FIELDS + E18_FIELDS) if build == "count" else T_FIELDS
    if rp_split:
        expected = expected + (RP_COUNT_FIELDS if build == "count" else RP_TIME_FIELDS)
    for row in rows[1:]:
        if row.get("t") != "cell":
            fail(path + ": unexpected row type " + str(row.get("t")))
        for k in CELL_BASE + STATE_FIELDS:
            if k not in row:
                fail(path + ": cell field missing: " + k)
        for side in ("int_e_", "int_i_", "win_e_", "win_i_"):
            for name in expected:
                if side + name not in row:
                    fail(path + ": field missing: " + side + name)
            if not rp_split:
                continue
            # Arithmetic the split cannot satisfy by accident: every record the packing
            # loop handled is either moved or a self-copy; the two segments never exceed
            # the parent clock they are carved out of, and sort/move see the same passes.
            if build == "count":
                if (row[side + "rp_moved_records"] + row[side + "rp_selfcopy_records"]
                        != row[side + "rp_records"]):
                    fail(path + ": rp records do not add up at " + side)
                if (row[side + "rp_moved_bytes"] + row[side + "rp_selfcopy_bytes"]
                        != row[side + "rp_bytes"]):
                    fail(path + ": rp bytes do not add up at " + side)
            else:
                if row[side + "w_rp_sort_calls"] != row[side + "w_rp_move_calls"]:
                    fail(path + ": rp sort/move call counts differ at " + side)
                if row[side + "w_rp_sort_calls"] > row[side + "w_reclaim_calls"]:
                    fail(path + ": rp full passes exceed reclaim calls at " + side)
                if (row[side + "w_reclaim_ns"]
                        < row[side + "w_rp_sort_ns"] + row[side + "w_rp_move_ns"]):
                    fail(path + ": rp segments exceed the parent clock at " + side)
    return build
'''

RP_HELPERS = '''def _rp_per_call(rows, ns, calls_field, calib):
    """Per-full-pass nanoseconds: raw and corrected by ONE calibration pair. Each segment
    is delimited by a single shared boundary, so one pair is its own instrument cost.
    Returns (raw, corrected, median calls)."""
    raw, corr, calls = [], [], []
    for r in rows:
        c = r["win_i_" + calls_field]
        calls.append(c)
        if c > 0:
            raw.append(r["win_i_" + ns] / c)
            corr.append(r["win_i_" + ns] / c - calib)
    if not raw:
        return 0.0, 0.0, med(calls)
    return med(raw), med(corr), med(calls)


def rp_time_group(rows, calib):
    def m(key):
        return med([r["win_i_" + key] for r in rows])
    cycles = max(1.0, med([r["win_cycles"] for r in rows]))
    s_raw, s_corr, _ = _rp_per_call(rows, "w_rp_sort_ns", "w_rp_sort_calls", calib)
    m_raw, m_corr, _ = _rp_per_call(rows, "w_rp_move_ns", "w_rp_move_calls", calib)
    t_raw, t_corr, _ = _rp_per_call(rows, "w_reclaim_ns", "w_reclaim_calls", 2.0 * calib)
    fs = " [AT FLOOR]" if s_raw <= calib else ""
    fm = " [AT FLOOR]" if m_raw <= calib else ""
    tot = s_corr + m_corr
    share = (100.0 * m_corr / tot) if tot > 0 else 0.0
    print("    repack split (insert side, per full pass; one calib pair removed per segment,"
          " the parent by two approximate pairs):")
    print("      reclaim calls %.3f/op; full passes %.3f/op; sort %8.1f -> %8.1f%s ns; "
          "move %8.1f -> %8.1f%s ns; parent %8.1f -> %8.1f ns; move share %5.1f%%"
          % (m("w_reclaim_calls") / cycles, m("w_rp_sort_calls") / cycles,
             s_raw, s_corr, fs, m_raw, m_corr, fm, t_raw, t_corr, share))
    ceiling = (m("w_reclaim_calls") / cycles) * m_corr
    print("      H ceiling before its own find cost: %.1f ns/op = %.3f calls/op x %.1f ns"
          " move/call" % (ceiling, m("w_reclaim_calls") / cycles, m_corr))


def rp_count_group(rows):
    def m(key):
        return med([r["win_i_" + key] for r in rows])
    cycles = max(1.0, med([r["win_cycles"] for r in rows]))
    calls = m("rp_calls")
    per = (lambda x: (m(x) / calls) if calls > 0 else 0.0)
    perop = (lambda x: m(x) / cycles)
    print("    repack work (insert side): %.3f full passes/op; per pass: %6.2f records, "
          "%8.1f B memmove'd (moved %6.2f rec / %8.1f B; self-copy %6.2f rec / %8.1f B)"
          % (calls / cycles, per("rp_records"), per("rp_bytes"), per("rp_moved_records"),
             per("rp_moved_bytes"), per("rp_selfcopy_records"), per("rp_selfcopy_bytes")))
    print("      per op: %6.2f records, %8.1f B moved (offset changed)"
          % (perop("rp_records"), perop("rp_moved_bytes")))
'''

SUMMARY_TIME_EXTRA = '''        if hdr.get("rp_split") == 1:
            s_raw, s_corr, _ = _rp_per_call(rows, "w_rp_sort_ns", "w_rp_sort_calls", calib)
            m_raw, m_corr, _ = _rp_per_call(rows, "w_rp_move_ns", "w_rp_move_calls", calib)
            tot = s_corr + m_corr
            print("                    | rp split: sort %7.1f move %7.1f ns/call (corr); "
                  "move share %5.1f%%; calls %.3f/op"
                  % (s_corr, m_corr, (100.0 * m_corr / tot) if tot > 0 else 0.0,
                     op("i", "w_reclaim_calls")))
'''

SUMMARY_COUNT_EXTRA = '''        if hdr.get("rp_split") == 1:
            rpc = _wmed(rows, "win_i_rp_calls", w)
            per = (lambda x: rpc and _wmed(rows, "win_i_" + x, "win_i_rp_calls"))
            print("                    | rp work/pass: %6.2f rec, %7.1f B; moved %6.2f "
                  "rec/%7.1f B; self-copy %6.2f rec/%7.1f B; passes/op %.3f"
                  % (per("rp_records"), per("rp_bytes"), per("rp_moved_records"),
                     per("rp_moved_bytes"), per("rp_selfcopy_records"),
                     per("rp_selfcopy_bytes"), rpc))
'''

NEW_MAIN = '''def main(argv):
    usage = ("usage: analyze_write_residual.py [--summary] [--require-rp-split] "
             "PATH [PATH ...]")
    if len(argv) < 2:
        fail(usage)
    summary = False
    require_split = False
    args = argv[1:]
    while args and args[0].startswith("--"):
        if args[0] == "--summary":
            summary = True
        elif args[0] == "--require-rp-split":
            require_split = True
        else:
            fail("unknown option: " + args[0])
        args = args[1:]
    if not args:
        fail(usage)
    pairs = {}
    for path in load_files(args):
        rows = read_rows(path)
        build = check_file(path, rows, require_split=require_split)
        hdr = rows[0]
        pairs.setdefault((hdr["backend"], hdr["kind"]), set()).add(build)
        cells = rows[1:]
        if summary:
            if build == "count":
                summary_count(path, hdr, cells)
            else:
                summary_time(path, hdr, cells)
        else:
            if build == "count":
                show_count(path, hdr, cells)
            else:
                show_time(path, hdr, cells)
    if require_split:
        for key in sorted(pairs):
            builds = pairs[key]
            if builds != {"count", "time"}:
                fail("--require-rp-split: %s/%s has files for %s, not one count+time pair"
                     % (key[0], key[1], "/".join(sorted(builds))))
    print()
    print("analyze_write_residual: done")
'''

OLD_MAIN = '''def main(argv):
    if len(argv) < 2:
        fail("usage: analyze_write_residual.py [--summary] PATH [PATH ...]")
    summary = False
    args = argv[1:]
    if args and args[0] == "--summary":
        summary = True
        args = args[1:]
    if not args:
        fail("usage: analyze_write_residual.py [--summary] PATH [PATH ...]")
    for path in load_files(args):
        rows = read_rows(path)
        build = check_file(path, rows)
        cells = rows[1:]
        if summary:
            if build == "count":
                summary_count(path, rows[0], cells)
            else:
                summary_time(path, rows[0], cells)
        else:
            if build == "count":
                show_count(path, rows[0], cells)
            else:
                show_time(path, rows[0], cells)
    print()
    print("analyze_write_residual: done")
'''

ANALYZER_SUBS = [
    ('w_findloc_ns w_findloc_calls w_walk_ns w_walk_calls w_fc_ns w_fc_calls""".split()',
     'w_findloc_ns w_findloc_calls w_walk_ns w_walk_calls w_fc_ns w_fc_calls""".split()\n'
     + RP_CONSTANTS),
    ('''def check_file(path, rows):
    hdr = rows[0]
    if hdr.get("t") != "hdr":
        fail(path + ": first row is not the header")
    for k in HDR_BASE:
        if k not in hdr:
            fail(path + ": header field missing: " + k)
    build = hdr["build"]
    expected = (E25_FIELDS + E18_FIELDS) if build == "count" else T_FIELDS
    for row in rows[1:]:
        if row.get("t") != "cell":
            fail(path + ": unexpected row type " + str(row.get("t")))
        for k in CELL_BASE + STATE_FIELDS:
            if k not in row:
                fail(path + ": cell field missing: " + k)
        for side in ("int_e_", "int_i_", "win_e_", "win_i_"):
            for name in expected:
                if side + name not in row:
                    fail(path + ": field missing: " + side + name)
    return build
''', RP_CHECK_FILE),
    ('def show_count(path, hdr, cells):', RP_HELPERS + '\n\ndef show_count(path, hdr, cells):'),
    ('''        st = {k: med([r[k] for r in rows]) for k in STATE_FIELDS}
        print("    state@checkpoint: live=%.0f overflow=%.0f compactions=%.0f "
              "index_bytes=%.0f regions=%.0f"''',
     '''        if hdr.get("rp_split") == 1:
            rp_count_group(rows)
        st = {k: med([r[k] for r in rows]) for k in STATE_FIELDS}
        print("    state@checkpoint: live=%.0f overflow=%.0f compactions=%.0f "
              "index_bytes=%.0f regions=%.0f"'''),
    ('''        st = {k: med([r[k] for r in rows]) for k in STATE_FIELDS}
        print("    state@checkpoint: live=%.0f overflow=%.0f compactions=%.0f "
              "index_bytes=%.0f" % (st["st_live"], st["st_overflow"], st["st_compactions"],''',
     '''        if hdr.get("rp_split") == 1:
            rp_time_group(rows, calib)
        st = {k: med([r[k] for r in rows]) for k in STATE_FIELDS}
        print("    state@checkpoint: live=%.0f overflow=%.0f compactions=%.0f "
              "index_bytes=%.0f" % (st["st_live"], st["st_overflow"], st["st_compactions"],'''),
    ('''        print("                    | walks %5.1f (fc %5.1f) ns/op; calls %.2f + %.2f /op"
              % (op("i", "w_walk_ns"), op("i", "w_fc_ns"), op("i", "w_walk_calls"),
                 op("i", "w_fc_calls")))
''', '''        print("                    | walks %5.1f (fc %5.1f) ns/op; calls %.2f + %.2f /op"
              % (op("i", "w_walk_ns"), op("i", "w_fc_ns"), op("i", "w_walk_calls"),
                 op("i", "w_fc_calls")))
''' + SUMMARY_TIME_EXTRA),
    ('''        print("                    | era/op cmp=%.2f cmpB=%.0f sh=%.1f relP=%.1f relO=%.2f "
              "ovR=%.2f ovB=%.0f free=%.2f | ov=%.0f MB=%.2f"
              % (era["cmp"], era["cmpB"], era["sh"], era["relP"], era["relO"], era["ovR"],
                 era["ovB"], era["free"], st_ov, st_mb))
''', '''        print("                    | era/op cmp=%.2f cmpB=%.0f sh=%.1f relP=%.1f relO=%.2f "
              "ovR=%.2f ovB=%.0f free=%.2f | ov=%.0f MB=%.2f"
              % (era["cmp"], era["cmpB"], era["sh"], era["relP"], era["relO"], era["ovR"],
                 era["ovB"], era["free"], st_ov, st_mb))
''' + SUMMARY_COUNT_EXTRA),
    (OLD_MAIN, NEW_MAIN),
]
RUNNER = r'''#!/usr/bin/env bash
# Repack split driver (2026-10-11): ONE bounded 8-cell pass on the W configuration.
#   cells  = {ctx16, lits} x {mixed/same 131072, tranco/grow 262144} x {25%, 200%}
#   builds = diag_write_residual (rp_* work counters) and diag_write_residual_t (the
#            w_rp_sort / w_rp_move split clocks), both rebuilt HERE from current sources:
#            a stale binary would carry the old field set, and the analyzer now refuses
#            missing fields by name.
# Refuses to overwrite any output or the log; stops with a nonzero exit at the first
# failing step (build, run, analyze). The ALLDONE marker is never the success test.
set -u
cd "$(dirname "$0")"
outdir=${1:-results/repack_split_20261011}
log=${2:-logs/repack_split_20261011.log}

if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
if [ -e "$outdir" ]; then
  echo "REFUSED: $outdir already exists (the round's outputs are immutable)" >&2
  exit 2
fi

mkdir -p "$outdir"
: > "$log"

echo "### host: $(uname -srm)" | tee -a "$log"
echo "### cpu: $(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //')" | tee -a "$log"
echo "### nproc: $(nproc)" | tee -a "$log"
echo "### source identities:" | tee -a "$log"
for f in index.hpp diag_write_residual.cpp analyze_write_residual.py run_repack_split.sh; do
  sha256sum "$f" | tee -a "$log"
done

for t in diag_write_residual diag_write_residual_t; do
  echo "### building $t" | tee -a "$log"
  python3 tools_build.py "$t" >> "$log" 2>&1
  rc=$?
  echo "### build rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then
    echo "BUILD FAILED: $t (see $log)" >&2
    exit 1
  fi
done

for t in diag_write_residual diag_write_residual_t; do
  echo "### build record $t:" | tee -a "$log"
  sed -n '1,12p' "logs/build_${t}.json" | tee -a "$log"
  sha256sum "build/${t}.exe" | tee -a "$log"
done

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

run_cell() {
  build="$1"; kind="$2"; n="$3"; csv="$4"; mode="$5"; bk="$6"; tag="$7"
  out="${outdir}/${bk}_${mode}_${kind}_${tag}.jsonl"
  echo "### ${build} ${kind} ${n} reps=4 mode=${mode} backend=${bk} -> ${out}" | tee -a "$log"
  /usr/bin/time -f "  wall %e s  rss %M KB" \
    python3 tools_build.py run "$build" "$kind" "$n" 4 "$csv" "rs20261011" "$mode" "$bk" \
    "$out" "0.25,2.00" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then
    echo "SWEEP FAILED: ${build} ${bk}/${mode}/${kind}; no summary is produced" >&2
    exit 1
  fi
}

for bk in ctx16 lits; do
  for build in diag_write_residual diag_write_residual_t; do
    tag=c
    [ "$build" = "diag_write_residual_t" ] && tag=t
    run_cell "$build" mixed 131072 "" "same" "$bk" "$tag"
    run_cell "$build" tranco 262144 "$CSV" "grow" "$bk" "$tag"
  done
done

echo "### sweep done" | tee -a "$log"
python3 analyze_write_residual.py --require-rp-split "$outdir" >> "$log" 2>&1
arc=$?
echo "### analyze rc=$arc" | tee -a "$log"
if [ "$arc" -ne 0 ]; then
  echo "ANALYZE FAILED (see $log)" >&2
  exit 1
fi
echo ALLDONE
'''


def expect(name, needle):
    b = (DIR / name).read_bytes()
    if needle.encode() not in b:
        print("POST-CHECK FAILED: %s lacks %r" % (name, needle), file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    rp = DIR / "run_repack_split.sh"
    if rp.exists():
        print("REFUSED: run_repack_split.sh already exists", file=sys.stderr)
        sys.exit(2)
    edit("index.hpp", INDEX_PRE, INDEX_SUBS)
    edit("diag_write_residual.cpp", DIAG_PRE, DIAG_SUBS)
    edit("analyze_write_residual.py", ANALYZER_PRE, ANALYZER_SUBS)
    rp.write_text(RUNNER, encoding="utf-8", newline="\n")
    print("run_repack_split.sh: written, %s" % sha(rp.read_bytes())[:12])
    expect("index.hpp", "rp_selfcopy_bytes")
    expect("index.hpp", "w_rp_move_ns")
    expect("diag_write_residual.cpp", '\\"rp_split\\":1')
    expect("diag_write_residual.cpp", "F(w_rp_sort_ns)")
    expect("analyze_write_residual.py", "--require-rp-split")
    expect("run_repack_split.sh", "rs20261011")
    print("patch_repack_split_20261011: OK")
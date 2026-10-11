#!/usr/bin/env bash
# S-round unresolved-cell probe: per-call first-batch timings for B/W/S/WS on the cells the
# A/B flagged as noisy. 2 processes x 2 repetitions per grid. Refuses to overwrite.
set -u
cd "$(dirname "$0")"
log=logs/sround_probe_20261010.log
for s in xfs_ctx16_same_mixed xfs_lits_same_mixed xfs_ctx16_same_skew; do
  for f in "results/main/${s}.jsonl" "results/main/${s}_firstscan.jsonl"; do
    if [ -e "$f" ]; then echo "REFUSED: $f exists" >&2; exit 2; fi
  done
done
if [ -e "$log" ]; then echo "REFUSED: $log exists" >&2; exit 2; fi
: > "$log"

run() {
  stem="$1"; kind="$2"; n="$3"; csv="$4"; md="$5"; bk="$6"
  for p in 1 2; do
    echo "### $stem P$p" | tee -a "$log"
    python3 tools_build.py run bench_firstscan_s "$kind" "$n" 2 "$csv" "xf$p" "$md" "$bk" "$stem" >> "$log" 2>&1
    rc=$?
    echo "rc=$rc" | tee -a "$log"
    [ "$rc" -ne 0 ] && { echo "PROBE FAILED $stem P$p" >&2; exit 1; }
  done
}

run xfs_ctx16_same_mixed mixed 131072 ""     same ctx16
run xfs_lits_same_mixed  mixed 131072 ""     same lits
run xfs_ctx16_same_skew  skew  131072 ""     same ctx16

echo "sround probe done"
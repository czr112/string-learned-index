#!/usr/bin/env bash
# First-batch scan probe sweep (scan-entry round, 2026-10-10). Two flagged grids,
# 2 independent processes x 2 repetitions each; refuses to touch existing outputs.
set -u
cd "$(dirname "$0")"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv
log=logs/firstscan_sweep_20261010.log

for stem in fs_lits_same_mixed fs_ctx16_grow_tranco; do
  for f in "results/main/$stem.jsonl" "results/main/${stem}_firstscan.jsonl"; do
    if [ -e "$f" ]; then
      echo "REFUSED: $f already exists" >&2
      exit 2
    fi
  done
done

if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"

for p in 1 2; do
  echo "### fs lits/same/mixed P$p" | tee -a "$log"
  python3 tools_build.py run bench_firstscan mixed 131072 2 "" "fp$p" same lits fs_lits_same_mixed >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "FAILED at lits/same/mixed P$p" >&2; exit 1; }
done

for p in 1 2; do
  echo "### fs ctx16/grow/tranco P$p" | tee -a "$log"
  python3 tools_build.py run bench_firstscan tranco 262144 2 "$CSV" "fp$p" grow ctx16 fs_ctx16_grow_tranco >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "FAILED at ctx16/grow/tranco P$p" >&2; exit 1; }
done

echo "firstscan sweep done"
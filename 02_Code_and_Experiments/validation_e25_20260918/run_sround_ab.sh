#!/usr/bin/env bash
# Targeted A/B for candidate S (scan-entry round, 2026-10-10). Arms B / W / S / W+S,
# same binary, same schedule, per-process position rotation (the bench does it).
# Five independent processes x 4 balanced repetitions per grid. Refuses to overwrite.
set -u
cd "$(dirname "$0")"
log=logs/sround_ab_20261010.log
for stem in sc_lits_same_mixed sc_ctx16_grow_tranco; do
  for s in "sr_${stem}"; do
    if [ -e "results/main/${s}.jsonl" ]; then
      echo "REFUSED: results/main/${s}.jsonl already exists" >&2
      exit 2
    fi
  done
done
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

for p in 1 2 3 4 5; do
  echo "### sr lits/same/mixed P$p" | tee -a "$log"
  python3 tools_build.py run bench_sround mixed 131072 4 "" "sr$p" same lits sr_sc_lits_same_mixed >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "AB FAILED lits/same/mixed P$p" >&2; exit 1; }
done

for p in 1 2 3 4 5; do
  echo "### sr ctx16/grow/tranco P$p" | tee -a "$log"
  python3 tools_build.py run bench_sround tranco 262144 4 "$CSV" "sr$p" grow ctx16 sr_sc_ctx16_grow_tranco >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "AB FAILED ctx16/grow/tranco P$p" >&2; exit 1; }
done

echo "sround ab sweep done"
#!/usr/bin/env bash
# Stage B2 (scan-entry round) discriminator sweep driver, 2026-10-10.
# Three flagged grids x 3 independent processes x 4 balanced repetitions. Each process
# appends to the same per-grid output stem, exactly like run_combo_scancheck.sh. Refuses
# to start if any of its output files already exists: an evidence file is never appended
# to by accident.
set -u
cd "$(dirname "$0")"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv
log=logs/scanconsume_sweep_20261010c.log

for stem in sc_ctx16_grow_tranco sc_lits_grow_tranco sc_lits_same_mixed; do
  for f in "results/main/$stem.jsonl" "results/main/${stem}_scanconsume.jsonl"; do
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

# order: KIND N REPS CSV RUNTAG MODE BACKEND OUTSTEM
run_one() {
  grid="$1"; kind="$2"; n="$3"; csv="$4"; md="$5"; bk="$6"
  for p in 1 2 3; do
    echo "### bench_scanconsume $grid P$p" | tee -a "$log"
    python3 tools_build.py run bench_scanconsume "$kind" "$n" 4 "$csv" "sep$p" "$md" "$bk" "sc_$grid" >> "$log" 2>&1
    rc=$?
    echo "rc=$rc" | tee -a "$log"
    if [ "$rc" -ne 0 ]; then
      echo "SWEEP FAILED at $grid P$p" >&2
      exit 1
    fi
  done
}

run_one ctx16_grow_tranco tranco 262144 "$CSV" grow ctx16
run_one lits_grow_tranco  tranco 262144 "$CSV" grow lits
run_one lits_same_mixed   mixed  131072 ""     same lits

echo "scanconsume sweep done"
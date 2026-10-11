#!/usr/bin/env bash
# Candidate-H targeted A/B driver (2026-10-11): W vs W+H through the combo framework.
# Cells: {ctx16,lits} x {same/mixed 131072, grow/tranco 262144}. FIVE independent processes
# x 4 balanced repetitions; the arm order is rotated inside every repetition by the program
# itself, and W occupies two slots so a same-configuration contrast estimates the noise
# floor (analyze_h_ab.py). Refuses to overwrite; fail-fast on build/run/analyze.
# TIMING ROUND: no other experiment may run concurrently.
set -u
cd "$(dirname "$0")"
log=logs/h_ab_20261011.log
stems="hab_ctx16_same_mixed hab_lits_same_mixed hab_ctx16_grow_tranco hab_lits_grow_tranco"
for s in $stems; do
  if [ -e "results/main/${s}.jsonl" ]; then
    echo "REFUSED: results/main/${s}.jsonl exists" >&2
    exit 2
  fi
done
if [ -e "$log" ]; then echo "REFUSED: $log already exists" >&2; exit 2; fi
: > "$log"
CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

echo "### host: $(uname -srm)" | tee -a "$log"
echo "### building bench_combo_h" | tee -a "$log"
python3 tools_build.py bench_combo_h >> "$log" 2>&1
rc=$?
echo "build rc=$rc" | tee -a "$log"
[ "$rc" -ne 0 ] && { echo "BUILD FAILED" >&2; exit 1; }

run() {
  stem="$1"; kind="$2"; n="$3"; csv="$4"; mode="$5"; bk="$6"
  for p in 1 2 3 4 5; do
    echo "### $stem P$p" | tee -a "$log"
    /usr/bin/time -f "  wall %e s" python3 tools_build.py run bench_combo_h \
      "$kind" "$n" 4 "$csv" "hp$p" "$mode" "$bk" "$stem" >> "$log" 2>&1
    rc=$?
    echo "rc=$rc" | tee -a "$log"
    [ "$rc" -ne 0 ] && { echo "AB FAILED $stem P$p" >&2; exit 1; }
  done
}

run hab_ctx16_same_mixed   mixed  131072 ""     same ctx16
run hab_lits_same_mixed    mixed  131072 ""     same lits
run hab_ctx16_grow_tranco  tranco 262144 "$CSV" grow ctx16
run hab_lits_grow_tranco   tranco 262144 "$CSV" grow lits

echo "### analyzing" | tee -a "$log"
python3 analyze_h_ab.py >> "$log" 2>&1
rc=$?
echo "### analyze rc=$rc" | tee -a "$log"
[ "$rc" -ne 0 ] && { echo "ANALYZE FAILED (see $log)" >&2; exit 1; }
echo H_AB_DONE
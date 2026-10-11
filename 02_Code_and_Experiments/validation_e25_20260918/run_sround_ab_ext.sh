#!/usr/bin/env bash
# Extended same-mode A/B for candidate S (scan-entry round, 2026-10-10): the five cells
# that complete "two backends x three corpora" in the mode where the start-advance walk
# is long. Three independent processes x 4 balanced repetitions per grid. Refuses to
# overwrite; a separate log from run_sround_ab.sh.
set -u
cd "$(dirname "$0")"
log=logs/sround_ab_ext_20261010.log
for s in xsr_ctx16_same_mixed xsr_lits_same_skew xsr_ctx16_same_skew xsr_lits_same_tranco xsr_ctx16_same_tranco; do
  if [ -e "results/main/${s}.jsonl" ]; then
    echo "REFUSED: results/main/${s}.jsonl exists" >&2
    exit 2
  fi
done
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"
CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

run() {
  stem="$1"; kind="$2"; n="$3"; csv="$4"; mode="$5"; bk="$6"
  for p in 1 2 3; do
    echo "### $stem P$p" | tee -a "$log"
    python3 tools_build.py run bench_sround "$kind" "$n" 4 "$csv" "xs$p" "$mode" "$bk" "$stem" >> "$log" 2>&1
    rc=$?
    echo "rc=$rc" | tee -a "$log"
    [ "$rc" -ne 0 ] && { echo "AB FAILED $stem P$p" >&2; exit 1; }
  done
}

run xsr_ctx16_same_mixed   mixed  131072 ""     same ctx16
run xsr_lits_same_skew     skew   131072 ""     same lits
run xsr_ctx16_same_skew    skew   131072 ""     same ctx16
run xsr_lits_same_tranco   tranco 262144 "$CSV" same lits
run xsr_ctx16_same_tranco  tranco 262144 "$CSV" same ctx16

echo "sround ab ext done"
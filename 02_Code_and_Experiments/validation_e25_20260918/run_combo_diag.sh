#!/usr/bin/env bash
# Diagnostic twin of the TIMING benchmark. Same source, same workload shape, built with
# -DE25_DIAG, run once per (backend, mode, corpus) with 1 process x 4 repetitions. Its job
# is narrow and it is not a timing: it must show, on the exact workload shape the timing
# round will run, that the widened reclaim and the digest navigation actually executed --
# the clean timing build compiles the counters out, so it cannot answer that itself.
set -u
cd "$(dirname "$0")"
log=logs/diag_combo_20261010.log
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists; pass a new name in the script" >&2
  exit 2
fi
: > "$log"

run_diag() {
  bk="$1"; md="$2"; kind="$3"; n="$4"; csv="$5"
  stem="combo_diag_${bk}_${md}_${kind}"
  echo "### diag_combo $kind $n reps=4 $bk $md -> $stem" | tee -a "$log"
  python3 tools_build.py run diag_combo "$kind" "$n" 4 "$csv" diag "$md" "$bk" "$stem" >> "$log" 2>&1
  echo "rc=$?" | tee -a "$log"
}

for bk in ctx16 lits; do
  for md in same grow; do
    run_diag "$bk" "$md" mixed 131072 ""
    run_diag "$bk" "$md" skew 131072 ""
    run_diag "$bk" "$md" tranco 262144 ../validation_e5_20260913/data/tranco_XN6XN.csv
  done
done

echo "diag sweep done"
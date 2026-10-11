#!/usr/bin/env bash
# Stage D driver (2026-10-10): 8 cells x 2 builds, 1 process x 4 rebuild replays.
#   cells = {ctx16, lits} x {same/mixed 131072, grow/tranco 262144} x {25%, 200%}
#   builds = diag_write_residual (counts) and diag_write_residual_t (phase clocks)
# Refuses to overwrite any output or the log; stops at the first failure and does not
# summarize a partial sweep. Nothing is appended to a file this script did not create:
# the program itself also refuses an existing OUTPATH.
set -u
cd "$(dirname "$0")"
# Optional: [OUTDIR] [LOGNAME]. The defaults are the round final (post-instrumentation)
# generation; the pre-instrumentation pass used results/write_residual_20261010 and
# logs/write_residual_20261010.log and its files are on disk unchanged.
outdir=${1:-results/write_residual_20261010b}
log=${2:-logs/write_residual_20261010b.log}

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

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

echo "### host: $(uname -srm)" | tee -a "$log"
echo "### cpu: $(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //')" | tee -a "$log"
echo "### nproc: $(nproc)" | tee -a "$log"
for t in diag_write_residual diag_write_residual_t; do
  echo "### build record $t:" | tee -a "$log"
  sed -n '1,12p' "logs/build_${t}.json" | tee -a "$log"
  sha256sum "build/${t}.exe" | tee -a "$log"
done

run_cell() {
  build="$1"; kind="$2"; n="$3"; csv="$4"; mode="$5"; bk="$6"; tag="$7"
  out="${outdir}/${bk}_${mode}_${kind}_${tag}.jsonl"
  echo "### ${build} ${kind} ${n} reps=4 mode=${mode} backend=${bk} -> ${out}" | tee -a "$log"
  /usr/bin/time -f "  wall %e s  rss %M KB" \
    python3 tools_build.py run "$build" "$kind" "$n" 4 "$csv" "wr20261010" "$mode" "$bk" \
    "$out" "0.25,2.00" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then
    echo "SWEEP FAILED: ${build} ${bk}/${mode}/${kind}; no summary produced" >&2
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
python3 analyze_write_residual.py "$outdir" >> "$log" 2>&1
echo "### analyze rc=$?" | tee -a "$log"
echo ALLDONE

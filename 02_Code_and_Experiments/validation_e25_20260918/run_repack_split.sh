#!/usr/bin/env bash
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

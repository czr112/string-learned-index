#!/usr/bin/env bash
# Gate driver for the EqualDepthDirTrunc round (2026-10-11).
# Runs the new gate on three corpora at 131072, the sanitizer cells, and re-runs the
# five inherited gates/benchmarks that the previous round also rebuilt.
# Refuses to overwrite an existing log.
set -u
cd "$(dirname "$0")"
log="logs/truncdir_gate_20261011.log"
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

run() {
  target="$1"; shift
  echo "### $target $*" | tee -a "$log"
  python3 tools_build.py run "$target" "$@" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "GATE FAILED: $target $*" >&2; exit 1; }
}

build() {
  echo "### build $1" | tee -a "$log"
  python3 tools_build.py "$1" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "BUILD FAILED: $1" >&2; exit 1; }
}

build check_depth_dir_trunc
run check_depth_dir_trunc mixed 131072 ''
run check_depth_dir_trunc skew  131072 ''
run check_depth_dir_trunc tranco 131072 "$CSV"

export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
build san_check_depth_dir_trunc
echo "### san_check_depth_dir_trunc mixed 8192" | tee -a "$log"
python3 tools_build.py run san_check_depth_dir_trunc mixed 8192 '' >> "$log" 2>&1
echo "### san rc=$?" | tee -a "$log"
python3 tools_build.py run san_check_depth_dir_trunc skew 2048 '' >> "$log" 2>&1
echo "### san rc=$?" | tee -a "$log"
python3 tools_build.py run san_check_depth_dir_trunc tranco 4096 "$CSV" >> "$log" 2>&1
echo "### san rc=$?" | tee -a "$log"

# Inherited gates: rebuild all five, run the four runnable ones at a small size.
build check_g1_contract
build check_sround
build check_compact_routing
build check_hole_reuse
build bench_lits2x2
run check_g1_contract mixed 8192 ''
run check_sround mixed 8192 ''
run check_compact_routing mixed 8192 ''
run check_hole_reuse mixed 8192 '' ctx16

echo "truncdir gate sweep done"

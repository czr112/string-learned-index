#!/usr/bin/env bash
# P0 acceptance for the byte-order fix (2026-10-11): the new order gate, its sanitizer twin, and
# the two directory gates that now also carry the audit's counterexample.
# Refuses to overwrite an existing log.
set -u
cd "$(dirname "$0")"
log="logs/order_fix_gate_20261011.log"
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"
CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

build() {
  echo "### build $1" | tee -a "$log"
  python3 tools_build.py "$1" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then echo "BUILD FAILED: $1" >&2; exit 1; fi
}

run() {
  t="$1"; shift
  echo "### run $t $*" | tee -a "$log"
  python3 tools_build.py run "$t" "$@" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then echo "RUN FAILED: $t $*" >&2; exit 1; fi
}

build check_depth_dir_order
build check_depth_dir
build check_depth_dir_trunc

run check_depth_dir_order mixed 131072 ''
run check_depth_dir_order skew  131072 ''
run check_depth_dir_order tranco 131072 "$CSV"
run check_depth_dir mixed 131072 ''
run check_depth_dir skew  131072 ''
run check_depth_dir tranco 131072 "$CSV"
run check_depth_dir_trunc mixed 131072 ''
run check_depth_dir_trunc skew  131072 ''
run check_depth_dir_trunc tranco 131072 "$CSV"

export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
build san_check_depth_dir_order
build san_check_depth_dir
build san_check_depth_dir_trunc

run san_check_depth_dir_order mixed 8192 ''
run san_check_depth_dir_order skew  2048 ''
run san_check_depth_dir_order tranco 4096 "$CSV"
run san_check_depth_dir mixed 8192 ''
run san_check_depth_dir skew  2048 ''
run san_check_depth_dir tranco 4096 "$CSV"
run san_check_depth_dir_trunc mixed 8192 ''
run san_check_depth_dir_trunc skew  2048 ''
run san_check_depth_dir_trunc tranco 4096 "$CSV"

echo "order-fix gate sweep done" | tee -a "$log"

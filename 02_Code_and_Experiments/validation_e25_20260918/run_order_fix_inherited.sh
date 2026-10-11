#!/usr/bin/env bash
# Inherited-gate re-runs AFTER the P0 byte-order fix (2026-10-11), at the sizes the original
# rounds used. The sources changed (the model header), so every target is rebuilt first --
# `tools_build.py run` refuses a stale build rather than timing an old binary, which is the point.
set -u
cd "$(dirname "$0")"
log="logs/order_fix_inherited_20261011.log"
if [ -e "$log" ]; then echo "REFUSED: $log exists" >&2; exit 2; fi
: > "$log"
build() {
  echo "### build $1" | tee -a "$log"
  python3 tools_build.py "$1" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then echo "BUILD FAILED: $1" >&2; exit 1; fi
}
run() {
  echo "### run $*" | tee -a "$log"
  python3 tools_build.py run "$@" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then echo "RUN FAILED: $*" >&2; exit 1; fi
}
for t in check_g1_contract check_compact_routing check_sround check_hole_reuse bench_lits2x2; do
  build "$t"
done
run check_g1_contract mixed 131072 ''
run check_compact_routing mixed 131072 ''
run check_sround mixed 131072 ''
run check_hole_reuse mixed 131072 '' ctx16
echo "order-fix inherited sweep done" | tee -a "$log"

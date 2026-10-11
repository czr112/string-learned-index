#!/usr/bin/env bash
# Gate + entry driver for the BlockLinearRank round (2026-10-11).
# Three corpora x {Context16 vs BlockLinearRank} inside one run each, plus the sanitizer cell.
# Refuses to overwrite an existing log.
set -u
cd "$(dirname "$0")"
log="logs/blockrank_gate_20261011.log"
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

run() {
  echo "### check_block_rank $*" | tee -a "$log"
  python3 tools_build.py run check_block_rank "$@" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "GATE FAILED: $*" >&2; exit 1; }
}

run mixed 131072 ''
run skew  131072 ''
run tranco 131072 "$CSV"

export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
python3 tools_build.py san_check_block_rank >> "$log" 2>&1
echo "### san build rc=$?" | tee -a "$log"
python3 tools_build.py run san_check_block_rank mixed 8192 '' >> "$log" 2>&1
echo "### san run mixed 8192 rc=$?" | tee -a "$log"
python3 tools_build.py run san_check_block_rank skew 2048 '' >> "$log" 2>&1
echo "### san run skew 2048 rc=$?" | tee -a "$log"

echo "blockrank gate sweep done"

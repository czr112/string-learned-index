#!/usr/bin/env bash
# Gate driver for the scan-entry round's candidate S (2026-10-10).
# 3 corpora x 2 backends x same/grow, plus the sanitizer cells. Refuses to overwrite.
set -u
cd "$(dirname "$0")"
log="logs/sround_gate_20261010c.log"
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

run() {
  echo "### check_sround $*" | tee -a "$log"
  python3 tools_build.py run check_sround "$@" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  [ "$rc" -ne 0 ] && { echo "GATE FAILED: $*" >&2; exit 1; }
}

for md in same grow; do
  run mixed 131072 '' ctx16 "$md"
  run mixed 131072 '' lits  "$md"
  run skew  131072 '' ctx16 "$md"
  run skew  131072 '' lits  "$md"
  run tranco 131072 "$CSV" ctx16 "$md"
  run tranco 131072 "$CSV" lits  "$md"
done

export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
python3 tools_build.py san_check_sround >> "$log" 2>&1
echo "### san_check_sround build rc=$?" | tee -a "$log"
for bk in ctx16 lits; do
  echo "### san_check_sround mixed 8192 $bk same" | tee -a "$log"
  python3 tools_build.py run san_check_sround mixed 8192 '' "$bk" same >> "$log" 2>&1
  echo "rc=$?" | tee -a "$log"
done

echo "sround gate sweep done"
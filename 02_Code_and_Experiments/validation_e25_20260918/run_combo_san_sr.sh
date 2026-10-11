#!/usr/bin/env bash
# Combo-gate sanitizer re-check on the S identity (2026-10-10). New log name.
set -u
cd "$(dirname "$0")"
log=logs/san_combo_20261010_sr.log
if [ -e "$log" ]; then echo "REFUSED: $log exists" >&2; exit 2; fi
: > "$log"
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
for bk in ctx16 lits; do
  echo "### san_check_combo mixed 8192 $bk" | tee -a "$log"
  python3 tools_build.py run san_check_combo mixed 8192 '' "$bk" same >> "$log" 2>&1
  echo "rc=$?" | tee -a "$log"
done
echo "san recheck done"
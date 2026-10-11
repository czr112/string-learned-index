#!/usr/bin/env bash
# Sanitizer runs for the S round's gate binaries (2026-10-10). New log names.
set -u
cd "$(dirname "$0")"
log=logs/san_sround_20261010.log
if [ -e "$log" ]; then echo "REFUSED: $log exists" >&2; exit 2; fi
: > "$log"
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1

echo "### rebuild san_check_scan_entry" | tee -a "$log"
python3 tools_build.py san_check_scan_entry >> "$log" 2>&1
echo "build rc=$?" | tee -a "$log"
python3 tools_build.py run san_check_scan_entry >> "$log" 2>&1
echo "run rc=$?" | tee -a "$log"

for bk in ctx16 lits; do
  echo "### san_check_sround mixed 8192 $bk same" | tee -a "$log"
  python3 tools_build.py run san_check_sround mixed 8192 '' "$bk" same >> "$log" 2>&1
  echo "rc=$?" | tee -a "$log"
done

echo "san sround done"
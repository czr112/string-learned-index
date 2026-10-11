#!/usr/bin/env bash
# Sanitizer coverage for the scan-entry round's identity (2026-10-10).
# Same two cells and options as run_combo_san.sh; a NEW log name, because the
# round's discipline is that an evidence file is never overwritten.
set -u
cd "$(dirname "$0")"
log=logs/san_combo_20261010_se.log
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists; pass a new name in the script" >&2
  exit 2
fi
: > "$log"
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1

for bk in ctx16 lits; do
  echo "### san_check_combo mixed 8192 $bk" | tee -a "$log"
  python3 tools_build.py run san_check_combo mixed 8192 '' "$bk" same >> "$log" 2>&1
  echo "rc=$?" | tee -a "$log"
done

echo "san sweep done"
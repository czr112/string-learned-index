#!/usr/bin/env bash
# Sanitizer coverage for the combination gate. NOT a timing run: the binaries are -O1 with
# ASan's instrumentation and allocator, and the numbers they print are not measurements.
# The exit code of the gate still checks out: rc=0 means all checks passed with BOTH the
# widen and the digest paths exercised, rc=3 means the widened branch never ran on this
# (small) load and the sanitizer run covered everything else.
set -u
cd "$(dirname "$0")"
log=logs/san_combo_20261010.log
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
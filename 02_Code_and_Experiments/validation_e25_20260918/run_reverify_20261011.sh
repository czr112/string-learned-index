#!/usr/bin/env bash
# Identity re-verification after the Stage D supplementary patch (index.hpp 6aa6be5c...).
# The patch adds E25_DIAG counters, two E30_WRITE clocks and three switch-table entries;
# none of them is compiled into the shipped path, but the source identity changed, so the
# gates that pin arms through the switch table are rebuilt and re-run on THIS identity.
set -u
cd "$(dirname "$0")"
log=logs/reverify_20261011_d.log
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

echo "### builds" | tee -a "$log"
python3 tools_build.py check_combo check_sround check_scan_entry san_check_combo san_check_sround >> "$log" 2>&1
echo "build rc=$?" | tee -a "$log"

for md in same grow; do
  for k in mixed skew tranco; do
    for bk in ctx16 lits; do
      csv=""; [ "$k" = "tranco" ] && csv="$CSV"
      echo "### check_combo $k 131072 $bk $md" | tee -a "$log"
      python3 tools_build.py run check_combo "$k" 131072 "$csv" "$bk" "$md" >> "$log" 2>&1
      echo "rc=$?" | tee -a "$log"
      echo "### check_sround $k 131072 $bk $md" | tee -a "$log"
      python3 tools_build.py run check_sround "$k" 131072 "$csv" "$bk" "$md" >> "$log" 2>&1
      echo "rc=$?" | tee -a "$log"
    done
  done
done

export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
for bk in ctx16 lits; do
  echo "### san_check_combo mixed 8192 $bk" | tee -a "$log"
  python3 tools_build.py run san_check_combo mixed 8192 '' "$bk" same >> "$log" 2>&1
  echo "rc=$?" | tee -a "$log"
  echo "### san_check_sround mixed 8192 $bk" | tee -a "$log"
  python3 tools_build.py run san_check_sround mixed 8192 '' "$bk" same >> "$log" 2>&1
  echo "rc=$?" | tee -a "$log"
done

echo "### check_scan_entry (counter hand-check)" | tee -a "$log"
python3 tools_build.py run check_scan_entry >> "$log" 2>&1
echo "rc=$?" | tee -a "$log"

echo REVERIFY_DONE

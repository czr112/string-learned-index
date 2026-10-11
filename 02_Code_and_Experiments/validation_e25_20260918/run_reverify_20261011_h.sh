#!/usr/bin/env bash
# Identity re-verification after the candidate-H round (index.hpp 640660a3..., switches.hpp
# 5486909f...). The round adds the repack-split instrumentation and Config::primary_hole_reuse
# (default OFF) plus its registration in the four switch tables; the shipped configuration is
# unchanged, but the source identity changed, so the gates that pin arms through the switch
# table are rebuilt and re-run on THIS identity. Same coverage as run_reverify_20261011.sh:
# 12 combo cells, 12 S cells, four sanitizer runs, the scan-entry hand check.
set -u
cd "$(dirname "$0")"
log=logs/reverify_20261011_h.log
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists" >&2
  exit 2
fi
: > "$log"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

echo "### builds" | tee -a "$log"
python3 tools_build.py check_combo check_sround check_scan_entry san_check_combo san_check_sround >> "$log" 2>&1
brc=$?
echo "build rc=$brc" | tee -a "$log"
if [ "$brc" -ne 0 ]; then echo "BUILD FAILED" >&2; exit 1; fi

for md in same grow; do
  for k in mixed skew tranco; do
    for bk in ctx16 lits; do
      csv=""; [ "$k" = "tranco" ] && csv="$CSV"
      echo "### check_combo $k 131072 $bk $md" | tee -a "$log"
      python3 tools_build.py run check_combo "$k" 131072 "$csv" "$bk" "$md" >> "$log" 2>&1
      rc=$?
      echo "rc=$rc" | tee -a "$log"
      if [ "$rc" -ne 0 ]; then echo "COMBO FAILED $k $bk $md" >&2; exit 1; fi
      echo "### check_sround $k 131072 $bk $md" | tee -a "$log"
      python3 tools_build.py run check_sround "$k" 131072 "$csv" "$bk" "$md" >> "$log" 2>&1
      rc=$?
      echo "rc=$rc" | tee -a "$log"
      if [ "$rc" -ne 0 ]; then echo "SROUND FAILED $k $bk $md" >&2; exit 1; fi
    done
  done
done

export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
for bk in ctx16 lits; do
  echo "### san_check_combo mixed 8192 $bk" | tee -a "$log"
  python3 tools_build.py run san_check_combo mixed 8192 '' "$bk" same >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then echo "SAN COMBO FAILED $bk" >&2; exit 1; fi
  echo "### san_check_sround mixed 8192 $bk" | tee -a "$log"
  python3 tools_build.py run san_check_sround mixed 8192 '' "$bk" same >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  if [ "$rc" -ne 0 ]; then echo "SAN SROUND FAILED $bk" >&2; exit 1; fi
done

echo "### check_scan_entry (counter hand-check)" | tee -a "$log"
python3 tools_build.py run check_scan_entry >> "$log" 2>&1
rc=$?
echo "rc=$rc" | tee -a "$log"
if [ "$rc" -ne 0 ]; then echo "HANDCHECK FAILED" >&2; exit 1; fi

echo REVERIFY_H_DONE
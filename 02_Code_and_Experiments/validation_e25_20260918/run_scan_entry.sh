#!/usr/bin/env bash
# Stage A scan-entry round driver, 2026-10-10.
#
# Plan: docs/NEXT_ACTION_PLAN_20261010_SCAN.md, sections 3 and 5. One bounded entry
# diagnosis: rebuild under the CURRENT identity, run the counter hand-check and the
# correctness/sanitizer gates, then the three count grids the plan names, then the analyzer
# that refuses a broken grid. Counts only -- no timing claim is made from any file this
# script produces, and the diagnostic twin is not a timing build.
#
# The three grids (plan 3.1):
#   Context16 grow/tranco 262,144
#   LitsHpt   grow/tranco 262,144
#   LitsHpt   same/mixed  131,072   (the 25% cell where the regression is largest)
#
# Refusals: the log and every output stem must not exist beforehand. tools_build.py refuses
# to run a binary whose recorded sources no longer match the tree, so a stale build cannot be
# driven from here either -- that is why every target is rebuilt first.
set -u
cd "$(dirname "$0")"

log=logs/run_scan_entry_20261010.log
stems="combo_se_ctx16_growtranco combo_se_lits_growtranco combo_se_lits_samemixed"
if [ -e "$log" ]; then echo "REFUSED: $log exists" >&2; exit 2; fi
for s in $stems; do
  if [ -e "results/main/$s.jsonl" ] || [ -e "results/main/${s}_scanwork.jsonl" ]; then
    echo "REFUSED: results/main/$s*.jsonl exists; pass a new stem" >&2; exit 2
  fi
done
: > "$log"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

build() {
  echo "### build $*" | tee -a "$log"
  python3 tools_build.py "$@" >>"$log" 2>&1
  echo "rc=$?" | tee -a "$log"
}

run() {
  echo "### run $*" | tee -a "$log"
  python3 tools_build.py run "$@" >>"$log" 2>&1
  echo "rc=$?" | tee -a "$log"
}

# ---- 0. rebuild every binary this round uses, under the current source identity --------
build check_scan_entry
build san_check_scan_entry
build check_combo
build san_check_combo
build diag_combo

# ---- 1. the counter hand-check (target-loop verification) -------------------------------
run check_scan_entry

# ---- 2. correctness gates under the new identity ----------------------------------------
run check_combo tranco 262144 "$CSV" ctx16 grow
run check_combo tranco 262144 "$CSV" lits grow
run check_combo mixed 131072 '' lits same

# ---- 3. sanitizers -----------------------------------------------------------------------
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 run san_check_scan_entry
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 run san_check_combo mixed 8192 '' ctx16 same
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 run san_check_combo mixed 8192 '' lits same

# ---- 4. the three count grids --------------------------------------------------------------
run diag_combo tranco 262144 4 "$CSV" se grow ctx16 combo_se_ctx16_growtranco
run diag_combo tranco 262144 4 "$CSV" se grow lits combo_se_lits_growtranco
run diag_combo mixed 131072 4 '' se same lits combo_se_lits_samemixed

# ---- 5. the analyzer, on the grid it just produced -------------------------------------------
echo "### analyze_scan_entry" | tee -a "$log"
python3 -B analyze_scan_entry.py combo_se_ctx16_growtranco combo_se_lits_growtranco \
  combo_se_lits_samemixed >>"$log" 2>&1
echo "analyzer rc=$?" | tee -a "$log"

echo "scan-entry round done" | tee -a "$log"
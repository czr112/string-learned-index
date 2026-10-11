#!/usr/bin/env bash
# Bounded scan-regression diagnostic: the SAME workload shape, run through the diagnostic
# twin with scan counters attached, for the two cells where the scan regression is largest.
# One process x four repetitions per (backend, mode, corpus) -- this is NOT a timing run.
set -u
cd "$(dirname "$0")"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

for bk in ctx16 lits; do
  python3 tools_build.py run diag_combo tranco 262144 4 "$CSV" sd grow "$bk" "combo_scandiag_${bk}_growtranco" >/dev/null 2>&1
  echo "diag tranco grow $bk rc=$?"
done

python3 tools_build.py run diag_combo mixed 131072 4 "" sd same lits "combo_scandiag_lits_samemixed" >/dev/null 2>&1
echo "diag mixed same lits rc=$?"

echo scandiag done
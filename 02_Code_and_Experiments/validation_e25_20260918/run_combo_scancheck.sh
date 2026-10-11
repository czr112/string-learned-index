#!/usr/bin/env bash
# Independent reproduction sweep for the SCAN regression cells found by run combo_20261010a.
# Same binary, same schedule, new output stems, five fresh processes -- a regression that
# only exists in one process set is a device artifact, not a mechanism.
set -u
cd "$(dirname "$0")"

CSV=../validation_e5_20260913/data/tranco_XN6XN.csv

for bk in ctx16 lits; do
  for p in 1 2 3 4 5; do
    python3 tools_build.py run bench_combo tranco 262144 4 "$CSV" "sc-P$p-tranco" grow "$bk" "combo_scancheck_${bk}_grow" >/dev/null 2>&1
    echo "tranco grow $bk P$p rc=$?"
  done
done

for p in 1 2 3 4 5; do
  python3 tools_build.py run bench_combo mixed 131072 4 "" "sc-P$p-mixed" same lits "combo_scancheck_lits_same" >/dev/null 2>&1
  echo "mixed same lits P$p rc=$?"
done

echo scancheck done
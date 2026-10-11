#!/usr/bin/env bash
# Low-activity gate runs, 2026-10-10 (local), source index.hpp 44702000..., switches.hpp e75c7fad...
set -u
cd /mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/validation_e25_20260918
TR=../validation_e5_20260913/data/tranco_XN6XN.csv
{
echo "target: check_low_activity (timing build, -O3)"
for k in mixed skew tranco; do
  n=131072
  [ "$k" = "tranco" ] && n=262144
  echo "--- $k $n ---"
  ./build/check_low_activity.exe "$k" "$n" "$TR"
  echo "rc=$?"
done
echo "target: san_check_low_activity (ASan+UBSan, -O1 -g)"
for k in mixed skew tranco; do
  echo "--- san $k 20000 ---"
  ./build/san_check_low_activity.exe "$k" 20000 "$TR"
  echo "rc=$?"
done
} 2>&1 | tee results/main/low_activity_gate_20261010.txt
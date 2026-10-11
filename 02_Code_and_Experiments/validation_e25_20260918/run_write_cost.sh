#!/usr/bin/env bash
set -u
cd /mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/validation_e25_20260918
rm -f results/main/write_cost_diag_20261010.jsonl
for k in mixed skew; do for m in same insert; do echo "== $k $m"; /usr/bin/time -f "  wall %e s" python3 tools_build.py run diag_write_cost $k 262144 1 ../validation_e5_20260913/data/tranco_XN6XN.csv e3a $m 2>>logs/write_cost_run.log >/dev/null; done; done
for m in same insert; do echo "== tranco $m"; /usr/bin/time -f "  wall %e s" python3 tools_build.py run diag_write_cost tranco 262144 1 ../validation_e5_20260913/data/tranco_XN6XN.csv e3a $m 2>>logs/write_cost_run.log >/dev/null; done
echo "== mixed grow"; /usr/bin/time -f "  wall %e s" python3 tools_build.py run diag_write_cost mixed 262144 1 ../validation_e5_20260913/data/tranco_XN6XN.csv e3a grow 2>>logs/write_cost_run.log >/dev/null
echo ALLDONE

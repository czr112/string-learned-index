#!/usr/bin/env bash
# Canonical paired A/B for the TRUNCDIR round: main pair (C16 vs DDirT) and mechanism pair
# (DDir vs DDirT). Reuses the frozen device: 5 independent processes x 4 repetitions, arms
# rotating position inside each repetition, ratios paired within a repetition.
set -u
cd "$(dirname "$0")"
log="logs/truncdir_ab_20261011.log"
if [ -e "$log" ]; then echo "REFUSED: $log exists" >&2; exit 2; fi
: > "$log"

echo "### build bench_depth_dir" | tee -a "$log"
python3 tools_build.py bench_depth_dir >> "$log" 2>&1 || { echo "BUILD FAILED" >&2; exit 1; }

for pair in "truncdir_ab C16,DDirT" "truncdir_mech DDir,DDirT"; do
  stem="${pair%% *}"; arms="${pair##* }"
  echo "### run $stem arms=$arms" | tee -a "$log"
  python3 run_depth_dir_ab.py --run --stem "$stem" --arms "$arms" >> "$log" 2>&1 || { echo "RUN FAILED: $stem" >&2; exit 1; }
  echo "### analyze $stem" | tee -a "$log"
  python3 run_depth_dir_ab.py --analyze --stem "$stem" --arms "$arms" > "results/main/${stem}_analysis.json" 2>> "$log" || { echo "ANALYZE FAILED: $stem" >&2; exit 1; }
done
echo "truncdir A/B done" | tee -a "$log"

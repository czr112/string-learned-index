#!/bin/bash
# Complete the 65k coverage: URLs replacement and hotspot cells at 16 blocks.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"
F="$W/builds/s2_20261025"
T="$R/results/bm_audited_v3_20261025/traces"
[ -x "$F/pure" ] || { echo "missing driver"; exit 1; }
for cell in urls_95_5_uniform_growth urls_95_5_hotspot_growth \
            urls_80_20_uniform_growth urls_80_20_hotspot_growth \
            urls_95_5_uniform_replacement urls_95_5_hotspot_replacement \
            urls_80_20_uniform_replacement urls_80_20_hotspot_replacement; do
  : >"$F/${cell}.jsonl"
  for process in $(seq 0 15); do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$process" >>"$F/${cell}.jsonl" 2>>"$F/${cell}.err" \
      || { echo "FAILED $cell p$process"; tail -3 "$F/${cell}.err"; exit 1; }
  done
  echo "== $cell done"
done

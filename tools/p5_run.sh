#!/bin/bash
# Priority 5: independent sample cells plus a re-check of the one cell that exceeded the query gate.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"
F="$W/builds/p4_20261024"
T="$R/results/bm_audited_traces_20261024/traces"
[ -x "$F/pure" ] || { echo "missing driver"; exit 1; }
for cell in words_indep_95_5_uniform_growth words_indep_80_20_uniform_growth \
            words_indep_95_5_hotspot_growth words_indep_80_20_hotspot_growth \
            words_indep_95_5_uniform_replacement words_indep_80_20_uniform_replacement \
            words_80_20_uniform_replacement; do
  : >"$F/${cell}.jsonl"
  for process in 0 1 2 3 4 5 6 7; do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$process" >>"$F/${cell}.jsonl" 2>>"$F/${cell}.err" \
      || { echo "FAILED $cell p$process"; tail -3 "$F/${cell}.err"; exit 1; }
  done
  echo "== $cell done"
done

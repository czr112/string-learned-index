#!/bin/bash
# Step 4: 100k dataset, same audited generator and same four-arm protocol.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/prepare_bm_audited_v2.py')
t = p.read_text(encoding='utf-8')
old_start = t.index('DATASETS = {')
old_end = t.index('}', old_start) + 1
new = ("DATASETS = {\n"
       "    'words_100k': '07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/'\n"
       "                  'english_words_100000_s20261019_rows.bin',\n"
       "}")
t = t[:old_start] + new + t[old_end:]
t = t.replace("results' / 'bm_audited_v2_20261025'", "results' / 'bm_audited_100k_20261025'")
t = t.replace("        for ratio_name, ratio in (('95_5', 0.05), ('80_20', 0.20)):\n"
              "            for locality in ('uniform', 'hotspot'):",
              "        for ratio_name, ratio in (('95_5', 0.05), ('80_20', 0.20)):\n"
              "            for locality in ('uniform', 'hotspot'):")
p.write_text(t, encoding='utf-8')
print('100k dataset wired')
PY
rm -rf "$R/results/bm_audited_100k_20261025"
python3 -B "$D/prepare_bm_audited_v2.py" 2>&1 | tail -6
F="$W/builds/s2_20261025"
T="$R/results/bm_audited_100k_20261025/traces"
for cell in words_100k_95_5_uniform_growth words_100k_80_20_uniform_growth \
            words_100k_95_5_uniform_replacement words_100k_80_20_uniform_replacement; do
  : >"$F/${cell}.jsonl"
  for process in $(seq 0 7); do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$process" >>"$F/${cell}.jsonl" 2>>"$F/${cell}.err" \
      || { echo "FAILED $cell p$process"; tail -3 "$F/${cell}.err"; exit 1; }
  done
  echo "== $cell done"
done

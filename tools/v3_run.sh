#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/prepare_bm_audited_v2.py')
t = p.read_text(encoding='utf-8')
start = t.index('DATASETS = {')
end = t.index('}', start) + 1
new = ("DATASETS = {\n"
       "    'words': '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',\n"
       "    'urls': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_urls_rows.bin',\n"
       "    'words_indep': '07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/'\n"
       "                   'english_words_s20261019_rows.bin',\n"
       "}")
t = t[:start] + new + t[end:]
import re
t = re.sub(r"results' / 'bm_audited_[a-z0-9_]+'", "results' / 'bm_audited_v3_20261025'", t)
p.write_text(t, encoding='utf-8')
print('datasets and output dir restored')
PY
cd "$W"
rm -rf 07_Eval_Integration/results/bm_audited_v3_20261025
python3 -B 07_Eval_Integration/diagnostics/prepare_bm_audited_v2.py 2>&1 | tail -8

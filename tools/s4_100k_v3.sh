#!/bin/bash
# 100k coverage: regenerate the 100k traces with the paired generator, then 8 blocks on 8 cells
# and print the aggregated table.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
python3 - <<'PY'
import pathlib
p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/prepare_bm_audited_v2.py')
t = p.read_text(encoding='utf-8')
start = t.index('DATASETS = {')
end = t.index('}', start) + 1
new = ("DATASETS = {\n"
       "    'words_100k': '07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/'\n"
       "                  'english_words_100000_s20261019_rows.bin',\n"
       "}")
t = t[:start] + new + t[end:]
import re
t = re.sub(r"results' / 'bm_audited_[a-z0-9_]+'", "results' / 'bm_audited_100k_v3_20261025'", t)
p.write_text(t, encoding='utf-8')
print('100k generator configured')
PY
rm -rf "$R/results/bm_audited_100k_v3_20261025"
python3 -B "$D/prepare_bm_audited_v2.py" 2>&1 | tail -3
F="$W/builds/s2_20261025"
T="$R/results/bm_audited_100k_v3_20261025/traces"
for cell in words_100k_95_5_uniform_growth words_100k_95_5_hotspot_growth \
            words_100k_80_20_uniform_growth words_100k_80_20_hotspot_growth \
            words_100k_95_5_uniform_replacement words_100k_95_5_hotspot_replacement \
            words_100k_80_20_uniform_replacement words_100k_80_20_hotspot_replacement; do
  : >"$F/${cell}.jsonl"
  for process in $(seq 0 7); do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$process" >>"$F/${cell}.jsonl" 2>>"$F/${cell}.err" \
      || { echo "FAILED $cell p$process"; tail -3 "$F/${cell}.err"; exit 1; }
  done
done
python3 - <<'PY'
import json, pathlib, statistics
base = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/builds/s2_20261025')
cells = ['words_100k_95_5_uniform_growth','words_100k_95_5_hotspot_growth',
         'words_100k_80_20_uniform_growth','words_100k_80_20_hotspot_growth',
         'words_100k_95_5_uniform_replacement','words_100k_95_5_hotspot_replacement',
         'words_100k_80_20_uniform_replacement','words_100k_80_20_hotspot_replacement']
out = {}
print(f"{'cell':44s} {'find':>6s} {'drop':>6s} {'[lo,hi]':>15s} {'pure':>5s} gate")
for cell in cells:
    rows = [json.loads(l) for l in (base/f'{cell}.jsonl').read_text().splitlines() if l.strip()]
    by = {}
    for r in rows: by.setdefault(r['arm'], []).append(r)
    bm, sp = by['bm'], by['bm_split']
    fr = sorted(s['find_us']/b['find_us'] for b,s in zip(bm,sp))
    dr = sorted(1-s['write_us']/b['write_us'] for b,s in zip(bm,sp))
    thr = statistics.median(r['ops']/(r['pure_seg_us']/1e6) for r in sp)/statistics.median(r['ops']/(r['pure_seg_us']/1e6) for r in bm)
    gate = 'pass' if (statistics.median(dr) >= 0.15 and dr[0] > 0) else 'FAIL'
    out[cell] = {'find_median': round(statistics.median(fr),4), 'find_interval':[round(fr[0],4),round(fr[-1],4)],
                 'write_drop': round(statistics.median(dr),4), 'write_interval':[round(dr[0],4),round(dr[-1],4)],
                 'pure_throughput_ratio': round(thr,4), 'write_gate': gate}
    print(f"{cell:44s} {statistics.median(fr):6.3f} {statistics.median(dr)*100:5.1f}% "
          f"[{dr[0]*100:5.1f},{dr[-1]*100:5.1f}] {thr:5.2f} {gate}")
pathlib.Path(base/'summary_100k_v3.json').write_text(json.dumps(out, indent=2)+'\n')
print('gate failures:', [c for c,v in out.items() if v['write_gate']=='FAIL'])
print('find medians:', sorted(round(v['find_median'],3) for v in out.values()))
PY

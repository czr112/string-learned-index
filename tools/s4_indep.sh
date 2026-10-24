#!/bin/bash
# Complete the third dataset: time the words_indep cells from the v3 traces, 16 blocks.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"
F="$W/builds/s2_20261025"; T="$R/results/bm_audited_v3_20261025/traces"
[ -x "$F/pure" ] || { echo missing driver; exit 1; }
for cell in words_indep_95_5_uniform_growth words_indep_95_5_hotspot_growth \
            words_indep_80_20_uniform_growth words_indep_80_20_hotspot_growth \
            words_indep_95_5_uniform_replacement words_indep_95_5_hotspot_replacement \
            words_indep_80_20_uniform_replacement words_indep_80_20_hotspot_replacement; do
  : >"$F/${cell}.jsonl"
  for process in $(seq 0 15); do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$process" >>"$F/${cell}.jsonl" 2>>"$F/${cell}.err" \
      || { echo "FAILED $cell p$process"; tail -3 "$F/${cell}.err"; exit 1; }
  done
  echo "== $cell done"
done
python3 - <<'PY'
import json, pathlib, statistics
base = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/builds/s2_20261025')
cells = [p.stem for p in sorted(base.glob('words_indep_*.jsonl'))]
T95 = 2.131
out = {}
print(f"{'cell':46s} {'ratio':>6s} {'95% CI':>16s} {'gain':>6s} {'find':>6s} {'pure':>5s} gate")
for cell in cells:
    rows = [json.loads(l) for l in (base/f'{cell}.jsonl').read_text().splitlines() if l.strip()]
    by = {}
    for r in rows: by.setdefault(r['arm'], []).append(r)
    bm, sp = by['bm'], by['bm_split']
    bm.sort(key=lambda r: (r['process'], r['position']))
    sp.sort(key=lambda r: (r['process'], r['position']))
    ratios = [s['write_us']/b['write_us'] for b, s in zip(bm, sp)]
    n = len(ratios); mean = statistics.fmean(ratios); sd = statistics.stdev(ratios)
    half = T95*sd/(n**0.5)
    fr = statistics.median(s['find_us']/b['find_us'] for b, s in zip(bm, sp))
    thr = statistics.median(r['ops']/(r['pure_seg_us']/1e6) for r in sp)/statistics.median(r['ops']/(r['pure_seg_us']/1e6) for r in bm)
    gate = 'PASS' if mean+half < 0.85 else 'fail'
    out[cell] = {'n': n, 'mean_ratio': round(mean,4), 'ci95': [round(mean-half,4), round(mean+half,4)],
                 'gain': round(1-mean,4), 'find_ratio': round(fr,4), 'pure_throughput_ratio': round(thr,4),
                 'gate': gate}
    print(f"{cell:46s} {mean:6.3f} [{mean-half:6.3f},{mean+half:6.3f}] {(1-mean)*100:5.1f}% {fr:6.3f} {thr:5.2f} {gate}")
pathlib.Path(base/'summary_indep.json').write_text(json.dumps(out, indent=2)+'\n')
print('failures:', [c for c,v in out.items() if v['gate']=='fail'])
PY

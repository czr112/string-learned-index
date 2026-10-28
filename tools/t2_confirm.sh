#!/bin/bash
# 16-block re-test of the T2 single-lookup change on the 65k growth cells, compared with the
# pre-change paired means recorded in the earlier summaries.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"
F="$W/builds/t2_20261029"
T="$R/results/bm_audited_v3_20261025/traces"
[ -x "$F/pure" ] || { echo missing driver; exit 1; }
for cell in words_95_5_uniform_growth words_95_5_hotspot_growth \
            words_80_20_uniform_growth words_80_20_hotspot_growth \
            urls_95_5_uniform_growth urls_95_5_hotspot_growth \
            urls_80_20_uniform_growth urls_80_20_hotspot_growth; do
  : >"$F/${cell}.jsonl"
  for p in $(seq 0 15); do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$p" >>"$F/${cell}.jsonl" 2>/dev/null
  done
  echo "== $cell"
done
python3 - <<'PY'
import json, pathlib, statistics
base = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/builds/t2_20261029')
old = {}
for f in ('builds/s2_20261025/summary_v3.json', 'builds/s2_20261025/summary_urls.json'):
    p = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace')/f
    if p.is_file():
        for cell, v in json.loads(p.read_text()).items():
            old[cell] = v['write_drop']
T95 = 2.131
print(f"{'cell':40s} {'old drop':>9s} {'new drop':>9s} {'delta':>7s} {'95% CI(ratio)':>16s}")
for path in sorted(base.glob('*_growth.jsonl')):
    cell = path.stem
    rows = [json.loads(l) for l in path.read_text().splitlines() if l.strip()]
    by = {}
    for r in rows: by.setdefault(r['arm'], []).append(r)
    bm, sp = by['bm'], by['bm_split']
    bm.sort(key=lambda r: (r['process'], r['position']))
    sp.sort(key=lambda r: (r['process'], r['position']))
    ratios = [s['write_us']/b['write_us'] for b, s in zip(bm, sp)]
    n = len(ratios); mean = statistics.fmean(ratios); sd = statistics.stdev(ratios)
    half = T95*sd/(n**0.5)
    prev = old.get(cell)
    new_drop = 1-mean
    delta = (new_drop - prev)*100 if prev is not None else float('nan')
    print(f"{cell:40s} {prev*100 if prev else 0:8.1f}% {new_drop*100:8.1f}% {delta:+6.1f}pp "
          f"[{mean-half:6.3f},{mean+half:6.3f}]")
PY

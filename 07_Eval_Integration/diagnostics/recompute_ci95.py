"""Recompute the 24-cell verdicts with a proper 95% paired interval (bootstrap + t), replacing
the min-max range that was reported earlier."""
import json
import pathlib
import statistics

dirs = {
    'words65': pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/results/bm_s1prime_20261025'),
    'urls65': pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/results/bm_s4_urls_20261025'),
    'words100': pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/results/bm_s4_100k_20261025'),
}
T95 = {7: 2.365, 8: 2.306, 15: 2.131, 16: 2.120}


def tcrit(n):
    return T95.get(n, 2.131)


rows = []
for group, base in dirs.items():
    for path in sorted(base.glob('*.jsonl')):
        cell = path.stem
        recs = [json.loads(l) for l in path.read_text().splitlines() if l.strip()]
        by = {}
        for r in recs:
            by.setdefault(r['arm'], []).append(r)
        bm, sp = by.get('bm'), by.get('bm_split')
        if not bm or not sp:
            continue
        # pair by process index (the blocks were run one process at a time, so order matters)
        bm.sort(key=lambda r: (r['process'], r['position']))
        sp.sort(key=lambda r: (r['process'], r['position']))
        pairs = list(zip(bm, sp))
        ratios = [s['write_us'] / b['write_us'] for b, s in pairs]
        n = len(ratios)
        mean = statistics.fmean(ratios)
        sd = statistics.stdev(ratios) if n > 1 else 0.0
        half = tcrit(n) * sd / (n ** 0.5) if n > 1 else 0.0
        rows.append({'group': group, 'cell': cell, 'blocks': n,
                     'median_ratio': round(statistics.median(ratios), 4),
                     'mean_ratio': round(mean, 4),
                     'ci95': [round(mean - half, 4), round(mean + half, 4)],
                     'min_max': [round(min(ratios), 4), round(max(ratios), 4)],
                     'gain_ge_15': (mean + half) < 0.85})

rows.sort(key=lambda r: (r['group'], r['cell']))
print(f"{'group':9s} {'cell':44s} {'n':>3s} {'median':>7s} {'95% CI':>17s} {'minmax':>15s} gate")
for r in rows:
    print(f"{r['group']:9s} {r['cell']:44s} {r['blocks']:3d} {r['median_ratio']:7.3f} "
          f"[{r['ci95'][0]:6.3f},{r['ci95'][1]:6.3f}] "
          f"[{r['min_max'][0]:6.3f},{r['min_max'][1]:6.3f}] "
          f"{'PASS' if r['gain_ge_15'] else 'fail'}")
pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/builds/ci95_24cells.json').write_text(
    json.dumps(rows, indent=2) + '\n')
print()
print('cells', len(rows), 'pass', sum(1 for r in rows if r['gain_ge_15']),
      'fail', sum(1 for r in rows if not r['gain_ge_15']))
print('failures:', [(r['group'], r['cell'], r['mean_ratio'], r['ci95']) for r in rows if not r['gain_ge_15']])

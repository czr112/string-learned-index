"""Post-run descriptive direct pairs; never multiply medians or change adoption gates."""
import hashlib
import json
from pathlib import Path
import statistics

root=Path(__file__).resolve().parents[1]
out=root/'results/bm_real_20261018'
assert json.loads((out/'independent_verification.json').read_text())['status']=='verified'
raw=json.loads((out/'raw.json').read_text());summary=json.loads((out/'summary.json').read_text());result=[]
for record in summary:
    rows=[r for r in raw if r['cell']==record['cell']];ratios={}
    for name in ('BM_base/ART','BM_base/F5','BM_base/directory','F5/ART'):
        a,b=name.split('/');points=[]
        for process in range(5):
            times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
            points.append(statistics.median(times[repeat,a]/times[repeat,b] for repeat in range(10)))
        band=max(record['aa_bands'][a],record['aa_bands'][b]);median=statistics.median(points)
        ratios[name]={'median':median,'process_medians':points,'faster':sum(x<1 for x in points),
                      'slower':sum(x>1 for x in points),'band':band,
                      'clear_faster_5pct':median<.95 and sum(x<1 for x in points)>=4 and 1/median>band,
                      'clear_slower_5pct':median>1.05 and sum(x>1 for x in points)>=4 and median>band}
    result.append({'cell':record['cell'],'ratios':ratios})
target=out/'additional_comparisons.json';assert not target.exists()
target.write_text(json.dumps({'analysis':'post-run descriptive direct pairs; previous and current adoption decisions unchanged',
                              'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                              'raw_sha256':hashlib.sha256((out/'raw.json').read_bytes()).hexdigest(),
                              'cells':result},indent=2)+'\n')
for r in result:print(r['cell'],{k:round(v['median'],3) for k,v in r['ratios'].items()})

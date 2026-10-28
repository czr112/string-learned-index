"""Independent ratio/AA/capacity/provenance verification of the native cost round."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import statistics as st

ROOT=Path(__file__).resolve().parents[1]
WS=ROOT.parent
CELLS=['english_words','memetracker_urls','memetracker_phrases','tranco64_norepl']
SLOTS=['legacy','cost','adaptive','ART','F5']
ARMS=[a for s in SLOTS for a in (s,s+'_AA')]
PAIRS=[('cost','legacy'),('adaptive','cost'),('adaptive','legacy'),('legacy','ART'),
       ('cost','ART'),('adaptive','ART'),('F5','ART')]+[(s+'_AA',s) for s in SLOTS]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def same(a,b):assert abs(a-b)<=1e-12*max(1,abs(a),abs(b)),(a,b)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('output');args=parser.parse_args()
    out=Path(args.output).resolve();identity=read(out/'identity.json')
    raw=read(out/'raw.json');builds=read(out/'builds.json');pg=read(out/'process_gates.json')
    gates=read(out/'gates.json');san=read(out/'gates_sanitized.json')
    summary=read(out/'summary.json');dec=read(out/'decision.json')
    assert not (out/'failure.json').exists()
    for path,digest in identity['sources'].items():assert sha(WS/path)==sha(out/'frozen_sources'/path)==digest,path
    for path,digest in identity['libraries'].items():assert sha(WS/path)==digest,path
    for path,digest in identity['artifacts'].items():assert sha(out/path)==digest,path
    for x in identity['inputs'].values():
        assert sha(WS/x['rows_path'])==x['rows_sha256'] and sha(WS/x['queries_path'])==x['queries_sha256']
    assert sha(out/'planner-costs.txt')==identity['profile_sha256']
    if 'reused_preflight' in identity:
        old=identity['reused_preflight'];previous=WS/old['path']
        assert not (previous/'raw.json').exists()
        assert sha(previous/'identity.json')==old['identity_sha256']
        assert sha(previous/'planner-costs.txt')==old['profile_sha256']==identity['profile_sha256']
        for name,digest in old['artifacts'].items():assert sha(previous/'build'/name)==sha(out/'build'/name)==digest
    lines=(out/'planner-costs.txt').read_text().splitlines()
    assert lines[0]=='blocklearn-query-cost/v1'
    profile=dict(line.split() for line in lines[1:])
    assert profile['origin']=='independent-synthetic-v1' and profile['target']=='x86_64-avx2' and profile['build']=='release'
    assert int(profile['iterations'])==32768
    for name,value in profile.items():
        if name not in ('origin','target','build','seed','iterations'):assert 0<float(value)<1e9
    archive=read(out/'log_archive.json')
    assert sha(out/'commands.log.gz')==archive['gzip_sha256']
    assert hashlib.sha256(gzip.decompress((out/'commands.log.gz').read_bytes())).hexdigest()==archive['raw_sha256']
    assert gates==san and [g['cell'] for g in gates]==CELLS
    for g in gates:
        assert g['rows']==identity['inputs'][g['cell']]['rows'] and g['queries']==identity['inputs'][g['cell']]['queries']
        assert [a['arm'] for a in g['arms']]==['legacy','cost','adaptive']
        for a in g['arms']:
            assert a['queries_counted']==g['queries'] and a['acceptance_checks']>0
            assert a['semantic_checks']>g['rows']+g['queries'] and a['scan_rows']>=g['rows']
            assert a['hash_leaves']+a['residual_leaves']<=a['leaves']
    assert len(raw)==2000 and len(builds)==100 and len(pg)==200
    assert [r['cell'] for r in summary]==CELLS
    for cell,s in zip(CELLS,summary):
        rows=[r for r in raw if r['cell']==cell];assert len(rows)==500
        for process in range(5):
            rr=[r for r in rows if r['process']==process];assert len(rr)==100
            for repeat in range(10):
                group=[r for r in rr if r['repeat']==repeat]
                assert len(group)==10 and {r['arm'] for r in group}==set(ARMS)
                assert {r['position'] for r in group}==set(range(10))
                assert len({r['checksum'] for r in group})==1
                assert all(r['elapsed_ns']>0 and r['operations']==identity['inputs'][cell]['queries'] for r in group)
            bs=[r for r in builds if r['cell']==cell and r['process']==process]
            gg=[r for r in pg if r['cell']==cell and r['process']==process]
            assert len(bs)==5 and {r['arm'] for r in bs}==set(SLOTS)
            assert len(gg)==10 and {r['arm'] for r in gg}==set(ARMS)
            assert all(r['verified']==identity['inputs'][cell]['rows']+identity['inputs'][cell]['queries'] for r in gg)
        for x,y in PAIRS:
            vals=[]
            for process in range(5):
                times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                vals.append(st.median(times[rep,x]/times[rep,y] for rep in range(10)))
            r=s['ratios'][x+'/'+y]
            assert vals==r['process_medians'];same(st.median(vals),r['median'])
            assert r['faster']==sum(v<1 for v in vals) and r['slower']==sum(v>1 for v in vals)
        band=max(max(v,1/v) for a in SLOTS for v in s['ratios'][a+'_AA/'+a]['process_medians'])
        same(band,s['aa_band'])
        for r in s['ratios'].values():
            assert r['clear_gain']==(r['median']<=.95 and r['faster']>=4 and 1/r['median']>band)
            assert r['clear_regression']==(r['median']>1.05 and r['slower']>=4 and r['median']>band)
            assert r['ten_percent_gain']==(r['median']<=.9 and r['faster']>=4 and 1/r['median']>band)
        for a in ARMS:same(s['ns_per_query'][a],st.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a))
        bs=[r for r in builds if r['cell']==cell];bt={(r['process'],r['arm']):r['elapsed_ns'] for r in bs}
        for a in ('cost','adaptive'):same(s['build_over_legacy'][a],st.median(bt[p,a]/bt[p,'legacy'] for p in range(5)))
        for a in SLOTS:
            for field in ('owned_bytes','retained_input_bytes'):
                assert {r[field] for r in bs if r['arm']==a}=={s[field][a]}
        for a in gates[CELLS.index(cell)]['arms']:
            assert s['owned_bytes'][a['arm']]==a['owned_bytes'] and s['retained_input_bytes'][a['arm']]==a['retained_input_bytes']
    for name,control in [('cost','legacy'),('adaptive','cost')]:
        d=dec[name];real=summary[:3]
        assert d['over_ART']=={s['cell']:s['ratios'][name+'/ART']['median'] for s in summary}
        assert d['over_comparator']=={s['cell']:s['ratios'][name+'/'+control]['median'] for s in summary}
        gains=[s['cell'] for s in real if s['ratios'][name+'/'+control]['ten_percent_gain']]
        regressions=[s['cell'] for s in real if s['ratios'][name+'/'+control]['clear_regression']]
        assert d['ten_percent_gain_real_cells']==gains and d['clear_regression_real_cells']==regressions
        assert d['cross_real_gain_without_regression']==(len(gains)>=2 and not regressions)
        assert d['three_real_cells_within_1_10_ART']==all(s['ratios'][name+'/ART']['median']<=1.1 for s in real)
        assert d['build_within_1_20_legacy']==all(s['build_over_legacy'][name]<=1.2 for s in real)
        assert d['total_capacity_within_1_10_legacy']==all(
            s['owned_bytes'][name]+s['retained_input_bytes'][name]<=1.1*(s['owned_bytes']['legacy']+s['retained_input_bytes']['legacy']) for s in real)
    assert dec['production_adopted'] is dec['default_changed'] is False
    result={'status':'verified','timing_rows':len(raw),'cells':4,'sanitized_cells':4,
            'source_files':len(identity['sources']),'profile_sha256':identity['profile_sha256'],
            'checks_per_build':sum(a['semantic_checks']+a['acceptance_checks'] for g in gates for a in g['arms']),
            'checks':['sources','frozen_sources','libraries','profile','artifacts','inputs','sanitized','raw_pairs','AA','capacity','decision']}
    (out/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':main()

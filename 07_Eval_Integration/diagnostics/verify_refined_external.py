"""Independently reconstruct E1 identities, balance, paired ratios and decisions."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import statistics
import sys

WORKSPACE=Path(__file__).resolve().parents[2]


def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main(out):
    meta=read(out/'identity.json')
    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source',path)
    for path,h in meta['artifacts'].items():assert sha(out/path)==h,('artifact',path)
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries_sha256']
    manifest=read(WORKSPACE/'07_Eval_Integration/adapters/our_static_refined/benchmark-adapter.json')['source_identity']
    frozen=read(WORKSPACE/'07_Eval_Integration/results/collision_refinement_20261018b/identity.json')
    for item in manifest['core_sources']:assert sha(WORKSPACE/item['path'])==item['sha256']==frozen['sources'][item['path']]
    bundle=hashlib.sha256(''.join(x['sha256'] for x in manifest['core_sources']).encode()).hexdigest()
    assert bundle==manifest['core_bundle_sha256']==meta['descriptor']['core_bundle']
    assert manifest['adapter_digest']==meta['descriptor']['adapter']
    report=read(out/'discovery/report.json');assert len(report['results'])==1 and report['results'][0]['status']=='passed'
    assert bundle in json.dumps(report['results']) and manifest['adapter_digest'] in json.dumps(report['results'])
    archive=read(out/'log_archive.json');assert sha(out/'commands.log.gz')==archive['gzip_sha256']
    data=gzip.decompress((out/'commands.log.gz').read_bytes());assert hashlib.sha256(data).hexdigest()==archive['raw_sha256']
    emitted=[json.loads(line) for line in data.decode().splitlines() if line.startswith('{')]
    gates=read(out/'gates.json');assert len(gates)==4
    for g in gates:
        assert g['sanitizer_rc']==g['sanitizer_stderr_bytes']==0
        assert all(g['normal'][k]==g['sanitized'][k] for k in ['checks','domain_checks','probes','scans','rejections','work_checks','rows','queries'])
        for d in [g['normal'],g['sanitized']]:
            assert d['work_checks']==d['queries']==100000 and d['checks']>30000000
            assert d['refinement_bytes']<=.05*(d['core_bytes']-d['refinement_bytes'])
    for folder,expected in [('our_static_refined','128/128'),('art','99/99'),('hot','99/99')]:
        assert expected in (out/(folder+'_acceptance.txt')).read_text()
    heads=read(out/'process_gates.json');raw=read(out/'raw.json');summaries=read(out/'summary.json')
    assert len(heads)==240 and len(raw)==1440 and len(summaries)==8
    strip=lambda r:{k:v for k,v in r.items() if k not in ['cell','process']}
    assert list(map(strip,heads))==[r for r in emitted if r.get('phase')=='gate']
    assert list(map(strip,raw))==[r for r in emitted if r.get('phase')=='paired']
    arms=['prototype','prototype_AA','integrated','integrated_AA','ART','HOT']
    pair_names={'integrated/prototype','integrated/ART','integrated/HOT','prototype/ART','prototype/HOT',
                'prototype_AA/prototype','integrated_AA/integrated'}
    expected_cells={f'{c}_{s}_{n}' for c,s in [('skew','full'),('mixed','full'),('tranco','full'),('tranco','64k')] for n in [10000,100000]}
    assert {s['cell'] for s in summaries}==set(meta['inputs'])==expected_cells
    count=0
    for s in summaries:
        cell=s['cell'];rows=[r for r in raw if r['cell']==cell];assert len(rows)==180 and len({r['checksum'] for r in rows})==1
        assert set(s['ratios'])==pair_names
        assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in rows)
        for p in range(5):
            hs=[g for g in heads if g['cell']==cell and g['process']==p];assert [g['arm'] for g in hs]==arms
            assert [g['object_id'] for g in hs]==[0,0,1,1,2,3]
            assert all(g['verified']==meta['inputs'][cell]['rows']+meta['inputs'][cell]['queries'] and g['retained_input_bytes']==0 for g in hs)
            for g in hs:
                assert g['build_position']==(g['object_id']-p)%4 and g['build_ns']>0
            assert {k:v for k,v in hs[0].items() if k!='arm'}=={k:v for k,v in hs[1].items() if k!='arm'}
            assert {k:v for k,v in hs[2].items() if k!='arm'}=={k:v for k,v in hs[3].items() if k!='arm'}
            assert hs[0]['index_bytes']==hs[2]['index_bytes']
            for g in hs[:2]:
                assert g['implementation_version']==meta['descriptor']['prototype_bundle'] and g['adapter_version']==meta['descriptor']['prototype_adapter']
            for g in hs[2:4]:
                assert g['implementation_version']==bundle and g['adapter_version']==manifest['adapter_digest']
            assert '301046804af165269e37da6725f5a4aec9ecc881' in hs[4]['implementation_version']
            assert meta['HOT_commit'] in hs[5]['implementation_version'] and hs[4]['index_bytes'] is None
            sample=[r for r in rows if r['process']==p];assert len(sample)==36;predecessors=collections.Counter()
            for rep in range(6):
                seq=sorted((r for r in sample if r['repeat']==rep),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(6))
                assert [r['arm'] for r in seq]==[arms[(x+rep+p)%6] for x in [0,1,5,2,4,3]]
                for a,b in zip(seq,seq[1:]):predecessors[a['arm'],b['arm']]+=1
            assert len(predecessors)==30 and set(predecessors.values())=={1}
            for arm in arms:assert sorted(r['position'] for r in sample if r['arm']==arm)==list(range(6))
        for name,r in s['ratios'].items():
            a,b=name.split('/');values=[]
            for p in range(5):
                times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==p}
                values.append(statistics.median(times[rep,a]/times[rep,b] for rep in range(6)));count+=6
            assert r['process_medians']==values and r['median']==statistics.median(values)
            assert r['faster']==sum(v<1 for v in values) and r['slower']==sum(v>1 for v in values)
        bands={name:max(max(v,1/v) for v in s['ratios'][name]['process_medians']) for name in ['prototype_AA/prototype','integrated_AA/integrated']}
        f=max(bands.values());assert s['aa_bands']==bands and s['observed_band']==f
        for r in s['ratios'].values():
            assert r['clear_gain']==(r['median']<=.95 and r['faster']>=4 and 1/r['median']>f)
            assert r['clear_regression']==(r['median']>1.05 and r['slower']>=4 and r['median']>f)
        for arm in arms:
            assert s['ns_per_op'][arm]==statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm)
        for arm in ['prototype','integrated','ART','HOT']:
            hs=[g for g in heads if g['cell']==cell and g['arm']==arm]
            assert s['build_ms_descriptive'][arm]==statistics.median(g['build_ns'] for g in hs)/1e6
            assert s['memory_by_process'][arm]==[{k:g[k] for k in ['index_bytes','adapter_bytes','retained_input_bytes']} for g in hs]
    decision=read(out/'decision.json');ratios={s['cell']:s['ratios']['integrated/ART']['median'] for s in summaries}
    qualifying=[c for c,r in ratios.items() if r<=1]
    assert decision['integration_regression_cells']==[s['cell'] for s in summaries if s['ratios']['integrated/prototype']['clear_regression']]
    assert decision['ART_within_10_percent_all_cells']==all(r<=1.10 for r in ratios.values())
    assert decision['ART_at_or_below_one_cells']==qualifying
    assert decision['advanced_target_met']==(len(qualifying)>=2 and any(c in qualifying for c in ['tranco_64k_10000','tranco_64k_100000','mixed_full_10000']))
    assert decision['ART_HOT_retested'] and not decision['production_adopted'] and decision['generalization_pending'] and decision['original_S3_failure_preserved']
    result={'verified':True,'cells':8,'gates':4,'process_gates':240,'timing_rows':1440,'paired_ratios':count,
        'sources':len(meta['sources']),'verification_source_sha256':sha(Path(__file__))}
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main(Path(sys.argv[1]).resolve())

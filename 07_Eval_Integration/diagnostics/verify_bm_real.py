"""Independently decode real inputs and recompute all paired timing decisions."""
import hashlib
import json
from pathlib import Path
import random
import statistics
import struct

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
STRATEGIES=['BM_base','BM','directory','F5','ART']
ARMS=[s+suffix for s in STRATEGIES for suffix in ('','_AA')]
PAIRS=['BM/BM_base','BM/directory','BM/F5','BM/ART']+[s+'_AA/'+s for s in STRATEGIES]

def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def decode(path,kind):
    data=path.read_bytes();pos=0
    if kind!='tli':
        assert data[:8]==(b'SIBROW1\0' if kind=='rows' else b'SIBQRY1\0');pos=8
    count,=struct.unpack_from('<Q',data,pos);pos+=8;items=[]
    for _ in range(count):
        if kind=='queries':
            present,value,n=struct.unpack_from('<BQQ',data,pos);pos+=17
            assert present in (0,1);key=data[pos:pos+n];pos+=n;items.append((key,bool(present),value))
        else:
            fmt='<I' if kind=='tli' else '<Q';n,=struct.unpack_from(fmt,data,pos);pos+=struct.calcsize(fmt)
            key=data[pos:pos+n];pos+=n
            if kind=='rows':value,=struct.unpack_from('<Q',data,pos);pos+=8;items.append((key,value))
            else:items.append(key)
    assert pos==len(data)
    return items

def main():
    out=ROOT/'results/bm_real_20261018';meta=read(out/'identity.json')
    assert meta['upstream_commit']=='e8e05411135b222edb82e3861799a2ae07ae5470'
    assert meta['benchmark_commit']=='003a55bf674b43350fbb70242070fb74f33a56ce'
    assert meta['fit']=={'block_size':1,'bytes':7,'segment_budget':8,'max_error':1}
    assert meta['sampling']=={'count':65536,'seed':42,'query_seed':20261018}
    assert meta['arms']==ARMS and meta['processes']==5 and meta['repeats']==10
    for path,digest in meta['sources'].items():
        assert sha(WORKSPACE/path)==sha(out/'frozen_sources'/path)==digest,('source drift',path)
    for path,digest in meta['reused_artifacts'].items():assert sha(WORKSPACE/path)==digest
    for path,item in meta['raw_sources'].items():
        assert (WORKSPACE/path).stat().st_size==item['bytes'] and sha(WORKSPACE/path)==item['sha256']
    for name in ('previous','semantic'):
        assert sha(WORKSPACE/meta[name+'_identity_path'])==meta[name+'_identity_sha256']
    assert not read(ROOT/'results/bm_latest_read_20261018/decision.json')['BM_optimization_gate_passed']
    assert list(meta['inputs'])==['english_words','memetracker_urls','memetracker_phrases']
    for name,item in meta['inputs'].items():
        assert item['rows']==65536 and item['queries']['count']==65536
        for path_key,hash_key in [('rows_path','rows_sha256'),('dataset_path','dataset_sha256'),('sidecar_path','sidecar_sha256')]:
            assert sha(WORKSPACE/item[path_key])==item[hash_key]
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
        info=read(WORKSPACE/item['sidecar_path']);assert info==item['dataset_provenance']
        keys=decode(WORKSPACE/item['dataset_path'],'tli');assert len(keys)==65536 and keys==sorted(set(keys))
        assert decode(WORKSPACE/item['rows_path'],'rows')==[(k,i) for i,k in enumerate(keys)]
        queries=[(k,True,i) for i,k in enumerate(keys)];random.Random(20261018).shuffle(queries)
        assert decode(WORKSPACE/item['queries_path'],'queries')==queries
        assert info['keys']==info['requested_keys']==65536 and info['seed']==42
        assert info['scan']['scan']=='complete source file'
        assert info['dataset_sha256']==item['dataset_sha256']
    corpus=read(out/'corpus_gates.json');gates=read(out/'gates.json');raw=read(out/'raw.json');summary=read(out/'summary.json')
    assert len(corpus)==3 and len(gates)==len(summary)==6 and len(raw)==3000
    for name,c in corpus.items():
        assert c['normal']==c['sanitized'] and c['normal'][-1]['status']=='passed'
        state=next(r for r in c['normal'] if r['phase']=='corpus')
        assert state['rows']==65536 and state['max_source_pages']<=2 and state['max_rebuild_rows']<=2049
        assert state['root_publications']==state['root_prepared']==state['rebuilds']+1
    logged=[json.loads(s) for s in (out/'commands.log').read_text(encoding='utf-8').splitlines() if s.startswith('{')]
    assert [r for r in logged if r['phase'] not in ('gate','pass')]==[r for c in corpus.values() for k in ('normal','sanitized') for r in c[k]]
    assert [r for r in logged if r['phase']=='gate']==[g[k] for g in gates for k in ('normal','sanitized')]+[g['normal'] for g in gates for _ in range(5)]
    assert [r for r in logged if r['phase']=='pass']==[{k:v for k,v in r.items() if k not in ('cell','process')} for r in raw]
    for record in summary:
        cell=record['cell'];rows=[r for r in raw if r['cell']==cell];assert len(rows)==500
        gate=next(g for g in gates if g['cell']==cell);g=gate['normal'];assert g==gate['sanitized']
        assert g['rows']==g['queries']==65536 and g['root_publications']==1 and g['BM_calls']>0
        assert g['BM_calls']+g['prefix_rejections']==65536 and g['optimized_blocks']<=g['BM_calls']
        assert g['writes']==(1024 if cell.endswith('/dirty') else 0)
        pairs={name:[] for name in PAIRS}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==100
            times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(times)==100
            for repeat in range(10):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(10))
                assert [r['arm'] for r in seq]==[ARMS[(x+repeat+process)%10] for x in (0,1,9,2,8,3,7,4,6,5)]
                assert all(r['elapsed_ns']>0 and r['operations']==65536 for r in seq)
                assert len({r['checksum'] for r in seq})==1
            for name in pairs:
                a,b=name.split('/');pairs[name].append(statistics.median(times[r,a]/times[r,b] for r in range(10)))
        bands={s:max(max(x,1/x) for x in pairs[s+'_AA/'+s]) for s in STRATEGIES};assert bands==record['aa_bands']
        for name,points in pairs.items():
            a,b=name.split('/');band=max(bands[a.removesuffix('_AA')],bands[b]);median=statistics.median(points)
            expected={'median':median,'process_medians':points,'faster':sum(x<1 for x in points),'slower':sum(x>1 for x in points),'band':band,
                      'clear_faster_5pct':median<.95 and sum(x<1 for x in points)>=4 and 1/median>band,
                      'clear_slower_5pct':median>1.05 and sum(x>1 for x in points)>=4 and median>band}
            assert record['ratios'][name]==expected
        r=record['ratios']['BM/BM_base']
        assert record['optimization_gain_passed']==(r['median']<=.90 and r['faster']>=4 and 1/r['median']>r['band'])
        assert record['optimization_risk_failed']==r['clear_slower_5pct']
        assert record['BM_ART_numerical_target_met']==(record['ratios']['BM/ART']['median']<=1.10)
        for arm,ns in record['ns_per_op'].items():assert ns==statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm)
    decision={'characterization_completed':True,'previous_optimization_gate_passed':False,'production_adopted':False,
              'dynamic_throughput_measured':False,'BM_ART_numerical_target_met_all':all(r['BM_ART_numerical_target_met'] for r in summary),
              'BM_ART_target_failures':[r['cell'] for r in summary if not r['BM_ART_numerical_target_met']]}
    assert read(out/'decision.json')==decision
    verification={'status':'passed','corpus_checks_per_build':sum(c['normal'][-1]['checks'] for c in corpus.values()),
                  'read_gate_checks_per_build':sum(g['normal']['checks'] for g in gates),'timing_rows':3000}
    assert read(out/'verification.json')==verification
    result={'status':'verified','source_to_interchange_lossless':True,'datasets':3,'cells':6,'timing_rows':3000,
            'original_BM_used':True,**decision,'verifier_sha256':sha(Path(__file__)),
            'commands_log_sha256':sha(out/'commands.log')}
    (out/'independent_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__':main()

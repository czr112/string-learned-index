"""Independent reconstruction of stage-4 counts, release invariants and pre-registered decisions."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parents[1];WS=ROOT.parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def main():
    p=argparse.ArgumentParser();p.add_argument('result',type=Path);a=p.parse_args();out=a.result.resolve()
    m=read(out/'identity.json')
    for rel,h in m['sources'].items():
        assert sha(WS/rel)==h,rel
        assert sha(out/'frozen_sources'/rel)==h,rel
    for rel,h in m['artifacts'].items():assert sha(out/rel)==h
    for rel,h in m['libraries'].items():assert sha(WS/rel)==h
    for x in m['inputs'].values():
        assert sha(WS/x['rows_path'])==x['rows_sha256'] and sha(WS/x['queries_path'])==x['queries_sha256']
    fixture=ROOT/'results/allocation_trace_fixture_20261019'
    assert sha(fixture/'identity.json')==m['fixture_identity_sha256'] and sha(fixture/'verification.json')==m['fixture_verification_sha256']
    assert not m['direct_bypass_calls']
    archive=read(out/'log_archive.json');assert sha(out/'commands.log.gz')==archive['gzip_sha256']
    assert hashlib.sha256(gzip.decompress((out/'commands.log.gz').read_bytes())).hexdigest()==archive['raw_sha256']
    cells=['english_words_s20261019','memetracker_urls_s20261019',
       'memetracker_phrases_s20261019','english_words_100000_s20261019'];arms=['bm_base','native','ART']
    contracts=read(out/'contracts.json');assert len(contracts)==6
    assert {(r['arm'],r['flavor']) for r in contracts}=={(x,y) for x in arms for y in ['clean','san']}
    assert all(r['rc']==0 and r['acceptance_checks']>=90 for r in contracts)
    gates=read(out/'gates.json');assert len(gates)==24
    assert {(r['arm'],r['cell'],r['flavor']) for r in gates}=={(x,y,z) for x in arms for y in cells for z in ['clean','san']}
    capacity=read(out/'capacity.json');timings=read(out/'build_times.json')
    assert len(capacity)==36 and len(timings)==60
    assert {(r['arm'],r['cell'],r['process']) for r in capacity}=={(x,y,z) for x in arms for y in cells for z in range(3)}
    assert {(r['arm'],r['cell'],r['process']) for r in timings}=={(x,y,z) for x in arms for y in cells for z in range(5)}
    fields=['rows','queries','hits','semantic_checks','checksum','input_heap_bytes','output_heap_bytes','self_report','retains_input']
    for cell in cells:
        rows=[r for r in gates+capacity+timings if r['cell']==cell]
        for key in ['rows','queries','hits','semantic_checks','checksum','input_heap_bytes']:
            assert len({r[key] for r in rows})==1,(cell,key)
        for arm in arms:
            selected=[r for r in rows if r['arm']==arm]
            assert all({k:r[k] for k in fields}=={k:selected[0][k] for k in fields} for r in selected)
        assert all(r['semantic_checks']==3*r['rows']+r['queries'] for r in rows)
        assert all(r['hits']==r['queries']-r['queries']//10 for r in rows)
    for r in gates+timings:
        assert not r['tracking_enabled'] and r['build_ns']>0
        assert all(r[k] is None for k in ['input_loaded','built','found','scanned','index_destroyed','input_destroyed','released'])
    for r in capacity:
        assert r['tracking_enabled'];b=r['built'];f=r['found'];s=r['scanned'];z=r['released']
        assert r['input_loaded']['input']['requested']==r['input_heap_bytes']
        assert b['input']['requested']==f['input']['requested']==s['input']['requested']==r['input_heap_bytes']
        assert b['index']['requested']==f['index']['requested']==s['index']['requested']>0
        assert f['query']['objects']==0 and f['query']['requested']==0
        if r['arm']=='bm_base':assert f['query']['allocations']==0
        assert s['scan']['requested']==r['output_heap_bytes']>0
        assert r['index_destroyed']['index']['objects']==0 and r['index_destroyed']['scan']['requested']==r['output_heap_bytes']
        assert r['input_destroyed']['input']['objects']==0 and r['input_destroyed']['scan']['requested']==r['output_heap_bytes']
        assert z['registry_live']==0
        for stage in ['input_loaded','built','found','scanned','index_destroyed','input_destroyed','released']:
            snap=r[stage];assert snap['registry_bytes']==33554432
            for domain in ['input','index','query','scan','test']:
                d=snap[domain]
                assert d['total_requested']-d['total_freed']==d['requested']
                assert d['total_usable']-d['freed_usable']==d['usable']>=d['requested']
                assert d['allocations']-d['frees']==d['objects']
                assert sum(d['kinds'])==d['allocations'] and d['failed']==0
            assert snap['registry_live']==sum(snap[d]['objects'] for d in ['input','index','query','scan','test'])
        for domain in ['input','index','query','scan','test']:
            assert z[domain]['requested']==z[domain]['usable']==z[domain]['objects']==0
        for stage in ['built','found','scanned']:
            snap=r[stage];effective=snap['index']['requested']+snap['query']['requested']+snap['scan']['requested']
            effective+=snap['input']['requested'] if r['retains_input'] else 0
            assert snap['peak_requested']>=effective
    summary=[]
    for cell in cells:
        per_arm={}
        for arm in arms:
            caps=[r for r in capacity if r['cell']==cell and r['arm']==arm];base=caps[0]
            def effective(r,usable=False):
                name='usable' if usable else 'requested'
                return r['built']['index'][name]+(r['built']['input'][name] if r['retains_input'] else 0)
            assert len({effective(r) for r in caps})==1
            assert len({r['built']['peak_requested'] for r in caps})==1
            times=[r['build_ns'] for r in timings if r['cell']==cell and r['arm']==arm]
            per_arm[arm]={'owned_requested':base['built']['index']['requested'],'retained_requested':base['input_heap_bytes'] if base['retains_input'] else 0,
                'resident_requested':effective(base),'resident_usable':statistics.median(effective(r,True) for r in caps),
                'build_peak_requested':base['built']['peak_requested'],'build_peak_usable':statistics.median(r['built']['peak_usable'] for r in caps),
                'find_peak_requested':base['found']['peak_requested'],'find_allocations':base['found']['query']['allocations'],
                'scan_peak_requested':base['scanned']['peak_requested'],'scan_output_requested':base['output_heap_bytes'],
                'clean_build_ns':times,'clean_build_median_ns':statistics.median(times),'self_report':base['self_report']}
        art=per_arm['ART']
        for arm in arms:
            r=per_arm[arm];r['resident_over_ART']=r['resident_requested']/art['resident_requested']
            r['build_peak_over_ART']=r['build_peak_requested']/art['build_peak_requested']
            r['build_time_over_ART']=r['clean_build_median_ns']/art['clean_build_median_ns']
            r['space_necessary']=r['resident_over_ART']<1;r['space_target']=r['resident_over_ART']<=.70
            r['scoped_entry_gate']=r['space_necessary'] and r['build_peak_over_ART']<=1 and r['build_time_over_ART']<=1.20
        summary.append({'cell':cell,'arms':per_arm})
    decision={'status':'stage4 ledger complete','production_adopted':False,'default_changed':False,
              'scoped_entry_cells':{arm:[r['cell'] for r in summary if r['arms'][arm]['scoped_entry_gate']] for arm in arms[:2]},
              'space_target_cells':{arm:[r['cell'] for r in summary if r['arms'][arm]['space_target']] for arm in arms[:2]},
              'query_speed_claim':'none; prior query entry failures remain closed','dynamic_stage_started':False}
    save(out/'summary.json',summary);save(out/'decision.json',decision)
    result={'status':'verified','source_files':len(m['sources']),'common_contract_runs':len(contracts),'corpus_gates':len(gates),
            'capacity_processes':len(capacity),'clean_build_processes':len(timings),'semantic_checks_total':sum(r['semantic_checks'] for r in gates+capacity+timings),
            'all_tracked_allocations_released':True,'BM_find_allocations':0,'capacity_requested_stable_3_of_3':True}
    save(out/'verification.json',result);print(json.dumps(result));print(json.dumps(decision))

if __name__=='__main__':main()

"""Audit frozen P3/P4 workloads and P5 bridge scope without rerunning performance."""
import argparse
import bisect
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
WS=ROOT.parent
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def rows(p):
    b=p.read_bytes();assert b[:8]==b'SIBROW1\0';n,=struct.unpack_from('<Q',b,8);pos=16;out=[]
    for _ in range(n):
        size,=struct.unpack_from('<Q',b,pos);pos+=8;k=b[pos:pos+size];pos+=size
        v,=struct.unpack_from('<Q',b,pos);pos+=8;out.append((k,v))
    assert pos==len(b);return out

def operations(p):
    b=p.read_bytes();assert b[:8]==b'SIBOPS1\0';n,=struct.unpack_from('<Q',b,8);pos=16;out=[]
    for _ in range(n):
        kind=b[pos];pos+=1;v,limit,size=struct.unpack_from('<QQQ',b,pos);pos+=24
        k=b[pos:pos+size];pos+=size;out.append((kind,k,v,limit))
    assert pos==len(b);return out

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='bm_p4_scope_audit_20261020');a=p.parse_args()
    assert Path(a.output).name==a.output
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False)
    p4=ROOT/'results/bm_p4_dynamic_20261020';meta=read(p4/'identity.json');record={r['cell']:r for r in read(p4/'records.json')}
    source_matches={rel:(WS/rel).exists() and sha(WS/rel)==digest for rel,digest in meta['sources'].items()}
    assert all(source_matches.values()),source_matches
    base=read(ROOT/'results/bm_bounded_build_20261019b/identity.json')
    source_keys={}
    for data in ['english_words','memetracker_urls','memetracker_phrases']:
        x=base['inputs'][data];path=WS/x['rows_path'];assert sha(path)==x['rows_sha256']
        source_keys[data]=[k for k,_ in rows(path)]
    audited=[];global_kinds=Counter()
    for cell,x in meta['inputs'].items():
        rp,op=WS/x['rows_path'],WS/x['operations_path']
        assert sha(rp)==x['rows_sha256'] and sha(op)==x['operations_sha256']
        initial=rows(rp);trace=operations(op);state=dict(initial);counts=Counter();new=dup=hits=0
        gen=x['generation'];hot_writes=total_writes=0
        keys=source_keys[x['data']]
        for kind,key,value,limit in trace:
            counts[str(kind)]+=1
            if kind!=1:
                total_writes+=1
                if x['locality']=='concentrated':
                    lo=gen['hot_page_start']*2048;hi=(gen['hot_page_start']+gen['hot_pages'])*2048
                    at=bisect.bisect_left(keys,key)
                    hot_writes+=lo<=at<min(hi,len(keys))
            if kind==1:hits+=key in state
            elif kind==2:
                if key in state:dup+=1
                else:state[key]=value;new+=1
            elif kind==3:
                if key in state:state[key]=value
            elif kind==4:state.pop(key,None)
            elif kind==5:pass
            else:raise AssertionError(('unknown kind',kind))
        r=record[cell]
        assert counts==Counter(x['kinds']) and len(trace)==r['operations']
        assert len(state)==r['final_size']==r['final_read_checked']==r['final_scan_rows']
        assert (hits,new,dup)==(r['find_hits'],r['insert_new'],r['insert_dup']) and r['mismatches']==0
        global_kinds.update(counts)
        audited.append({'cell':cell,'data':x['data'],'ratio':x['ratio'],'mode':x['mode'],'locality':x['locality'],
            'operations':len(trace),'kinds':dict(counts),'initial_rows':len(initial),'final_rows':len(state),
            'find_hit_fraction':hits/max(1,counts['1']),'insert_new':new,'insert_duplicates':dup,
            'insert_duplicate_fraction':dup/max(1,counts['2']),
            'write_targets_in_declared_hot_range':hot_writes/total_writes if x['locality']=='concentrated' else None,
            'write_us_with_oracle':r['write_us'],'rebuilds':r['rebuilds']})
    pairs=[]
    for data in source_keys:
        for ratio in ['95_5','80_20']:
            u=next(r for r in audited if r['data']==data and r['ratio']==ratio and r['mode']=='growth' and r['locality']=='uniform')
            c=next(r for r in audited if r['data']==data and r['ratio']==ratio and r['mode']=='growth' and r['locality']=='concentrated')
            xu,xc=meta['inputs'][u['cell']],meta['inputs'][c['cell']]
            pairs.append({'data':data,'ratio':ratio,'same_initial_rows_file':xu['rows_sha256']==xc['rows_sha256'],
                'uniform_new':u['insert_new'],'concentrated_new':c['insert_new'],
                'uniform_duplicates':u['insert_duplicates'],'concentrated_duplicates':c['insert_duplicates'],
                'uniform_find_hit_fraction':u['find_hit_fraction'],'concentrated_find_hit_fraction':c['find_hit_fraction'],
                'concentrated_actual_hot_write_fraction':c['write_targets_in_declared_hot_range']})
    assert global_kinds['4']==global_kinds['5']==0
    driver=(ROOT/'diagnostics/replay_bm_dynamic.cpp').read_text(encoding='utf-8')
    assert driver.index('const auto start=Clock::now();')<driver.index('const auto want=oracle.find(op.key);')<driver.index('const auto ns=')
    formal=(ROOT/'diagnostics/bm_page_adapter.hpp').read_text(encoding='utf-8')
    bounded=(ROOT/'diagnostics/bm_bounded_adapter.hpp').read_text(encoding='utf-8')
    assert 'return built().find(key);' in formal and 'std::vector<bm_page::Row> copied;' in formal
    assert 'index_->find_base(k)' in bounded and 'std::make_unique<bm_bounded_page::Index>(rows)' in bounded
    result={'status':'audited','cells':len(audited),'all_frozen_P4_sources_match':True,'all_trace_hashes_match':True,
        'all_record_counts_match_independent_replay':True,'global_operation_counts':dict(global_kinds),
        'cells_detail':audited,'growth_locality_pairs':pairs,
        'find_and_write_timing_includes_oracle':True,
        'hot_growth_claim_supported':False,
        'P5_scope':{'acceptance_preserved':read(ROOT/'results/bm_p5_20261020/identity.json')['acceptance'],
                    'tested_bridge':'bm_page_adapter.hpp; full row copy and Index::find',
                    'bounded_experimental_bridge':'bm_bounded_adapter.hpp; bounded constructor and find_base',
                    'same_candidate_identity':False,
                    'build_script_current_sha256':sha(WS/'tools/p5_rebuild.sh'),
                    'build_script_swallows_acceptance_exit':'|| true' in (WS/'tools/p5_rebuild.sh').read_text(encoding='utf-8')}}
    save(out/'audit.json',result)
    save(out/'identity.json',{'audit_script_sha256':sha(Path(__file__)),
        'P4_identity_sha256':sha(p4/'identity.json'),'P4_records_sha256':sha(p4/'records.json'),
        'P5_identity_sha256':sha(ROOT/'results/bm_p5_20261020/identity.json'),
        'derived_audit_sha256':sha(out/'audit.json'),'new_performance_measurement':False,'model_changed':False})
    print(json.dumps({'status':result['status'],'cells':len(audited),'global_operation_counts':dict(global_kinds),
        'growth_locality_pairs':pairs,'P5_scope':result['P5_scope']},ensure_ascii=False))

if __name__=='__main__':main()

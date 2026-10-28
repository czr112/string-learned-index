"""Independent BM source-origin, retained layout/transactions, semantic and timing audit."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
STRATEGIES=['BM_base','BM','directory','F5','ART','HOT']
ARMS=[s+suffix for s in STRATEGIES for suffix in ('','_AA')]
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def records(path):return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line.startswith('{')]
def identity(out,queries=False):
    meta=read(out/'identity.json')
    for path,digest in meta['sources'].items():
        assert sha(WORKSPACE/path)==digest,('source drift',path)
        assert sha(out/'frozen_sources'/path)==digest,('archive drift',path)
    for path,digest in meta['artifacts'].items():assert sha(out/path)==digest
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        if queries:assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    return meta
def function(source,signature):
    start=source.index(signature);begin=source.index('{',start);depth=0
    for end in range(begin,len(source)):
        if source[end]=='{':depth+=1
        if source[end]=='}':
            depth-=1
            if not depth:return source[start:end+1]
    raise AssertionError(signature)
def save(out,obj):
    obj.update(verifier_sha256=sha(Path(__file__)),commands_log_sha256=sha(out/'commands.log'))
    (out/'independent_verification.json').write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8');print(out.name,json.dumps(obj))
def main():
    parser=argparse.ArgumentParser();parser.add_argument('entry',type=Path);parser.add_argument('--timing',type=Path);args=parser.parse_args()
    out=args.entry.resolve();meta=identity(out)
    assert meta['upstream_commit']=='60837a886b7040828e6f0bb5382d7950d8eed4fa'
    assert meta['fit']=={'block_size':1,'bytes':7,'segment_budget':8,'max_error':1}
    for item in meta['upstream_identity']:
        path=WORKSPACE/'02_Code_and_Experiments/Learned-Index-Model'/item['path'];data=path.read_bytes()
        assert sha(path)==item['compiled_sha256'] and item['matches_fixed_git_blob']
        assert hashlib.sha256(data.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')).hexdigest()==item['historical_sha256']
    leaf=(ROOT/'diagnostics/bm_page_core.hpp').read_text(encoding='utf-8')
    old=(ROOT/'diagnostics/route_attribution_page.hpp').read_text(encoding='utf-8')
    retained_leaf=['std::string_view suffix(', 'std::string_view boundary(', 'std::uint32_t directory_block(',
                   'std::optional<std::uint64_t> verify_range(', 'std::vector<Row> scan(', 'Slot seek(',
                   'bool update(', 'bool erase(', 'Insert try_insert(', 'std::vector<std::string> fence_keys()']
    for signature in retained_leaf:assert function(leaf,signature)==function(old,signature),signature
    assert 'bm_=blocklearn::BlockModel::fit(training,1,7,8,1);' in leaf and 'const auto proposed=bm_.window(suffix);' in leaf
    assert 'train(r)' not in leaf and 'refinement_' not in leaf and 'model_window_x' not in leaf
    assert 'Page(Page&&)=delete' in leaf and 'arena_.capacity()-arena_.size()' in leaf
    owner=(ROOT/'diagnostics/bm_page_index.hpp').read_text(encoding='utf-8')
    old_owner=(ROOT/'diagnostics/coupled_page_index.hpp').read_text(encoding='utf-8')
    retained_owner=['std::unique_ptr<FlatRoot> prepare_root(', 'bool mark_dirty(', 'void clear_dirty(', 'Pages::iterator rebuild(',
                    'bool insert(', 'bool update(', 'bool erase(', 'std::vector<Row> scan(', 'void compact()', 'std::size_t verify()']
    for signature in retained_owner:assert function(owner,signature)==function(old_owner,signature),signature
    commands=meta['commands'];assert sum(any(str(x).endswith('/src/block_model.cpp') for x in c) for c in commands)==2
    gates=read(out/'gates.json');assert gates['normal']==gates['sanitized'] and len(gates['normal'])==4
    assert records(out/'commands.log')==[r for build in gates.values() for rows in build.values() for r in rows]
    total=0;work=[];space=[]
    for cell,rows in gates['normal'].items():
        assert rows[-1]['status']=='passed';total+=rows[-1]['checks']
        if cell=='synthetic':
            root=next(r for r in rows if r['phase']=='root_publication');assert root['failure_scenarios']==8 and root['binary_queries']==65536
            continue
        usage=next(r for r in rows if r['phase']=='bm_work');corpus=next(r for r in rows if r['phase']=='corpus')
        assert usage['queries']==2*meta['inputs'][cell]['rows'] and usage['bm_calls']>0
        assert usage['optimized_blocks']<=usage['base_blocks'] and usage['optimized_blocks']<=usage['bm_calls']
        assert corpus['root_publications']==corpus['root_prepared']==corpus['rebuilds']+1
        assert corpus['max_source_pages']<=2 and corpus['max_rebuild_rows']<=2049
        ratio=corpus['initial_slotted_bytes']/corpus['initial_d1_bytes'];space.append({'cell':cell,'BM_over_D1':ratio,'within_10pct':ratio<=1.10})
        work.append({'cell':cell,**usage})
    assert total==sum(read(out/'verification.json')['checks_per_build'].values())
    assert read(out/'entry_decision.json')=={'space_cells':space,'semantic_timing_allowed':True,'production_adopted':False}
    save(out,{'status':'verified','checks_per_build':total,'builds':2,'original_BM_used':True,'upstream_algorithm_unchanged':True,
              'retained_leaf_functions':retained_leaf,'retained_owner_functions':retained_owner,'work':work})
    if args.timing is None:return
    entry=out;out=args.timing.resolve();timing=identity(out,True)
    assert timing['entry_identity_sha256']==sha(entry/'identity.json') and timing['arms']==ARMS
    raw=read(out/'raw.json');summary=read(out/'summary.json');gates=read(out/'gates.json');logged=records(out/'commands.log')
    assert len(raw)==4320 and len(summary)==len(gates)==6
    assert [r for r in logged if r['phase']=='pass']==[{k:v for k,v in r.items() if k not in ('cell','process')} for r in raw]
    expected_gates=[g[k] for g in gates for k in ('normal','sanitized')]+[g['normal'] for g in gates for _ in range(5)]
    assert [r for r in logged if r['phase']=='gate']==expected_gates
    for folder in ('art','hot'):
        for kind in ('normal','sanitized'):
            accepted=(out/(folder+'_'+kind+'_acceptance.txt')).read_text(encoding='utf-8');assert '99/99 checks passed' in accepted and 'FAIL:' not in accepted
            config=read(out/'build'/(folder+'_'+kind)/'compile_commands.json')
            assert all('-march=x86-64-v3' in c['command'] for c in config)
            assert all(('-fsanitize=address,undefined' in c['command'])==(kind=='sanitized') for c in config)
    for record in summary:
        cell=record['cell'];rows=[r for r in raw if r['cell']==cell];assert len(rows)==720
        gate=next(g for g in gates if g['cell']==cell)
        for r in gate.values():
            if isinstance(r,dict):assert r['BM_calls']+r['prefix_rejections']==r['queries'] and r['root_publications']==1
        assert all(r['operations']==gate['normal']['queries'] and r['elapsed_ns']>0 for r in rows)
        pairs={name:[] for name in record['ratios']}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==144
            times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(times)==144
            for repeat in range(12):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(12))
                assert [r['arm'] for r in seq]==[ARMS[(x+repeat+process)%12] for x in (0,1,11,2,10,3,9,4,8,5,7,6)]
                assert len({r['checksum'] for r in seq})==1
            for name in pairs:
                a,b=name.split('/');pairs[name].append(statistics.median(times[r,a]/times[r,b] for r in range(12)))
        bands={s:max(max(x,1/x) for x in pairs[s+'_AA/'+s]) for s in STRATEGIES};assert bands==record['aa_bands']
        for name,points in pairs.items():
            a,b=name.split('/');band=max(bands[a.removesuffix('_AA')],bands[b]);median=statistics.median(points)
            expected={'median':median,'process_medians':points,'faster':sum(x<1 for x in points),'slower':sum(x>1 for x in points),'band':band,
                      'clear_faster_5pct':median<.95 and sum(x<1 for x in points)>=4 and 1/median>band,
                      'clear_slower_5pct':median>1.05 and sum(x>1 for x in points)>=4 and median>band}
            assert record['ratios'][name]==expected
        r=record['ratios']['BM/BM_base'];gain=r['median']<=.90 and r['faster']>=4 and 1/r['median']>r['band']
        assert record['optimization_gain_passed']==gain and record['optimization_risk_failed']==r['clear_slower_5pct']
        assert record['BM_ART_numerical_target_met']==(record['ratios']['BM/ART']['median']<=1.10)
        for arm,ns in record['ns_per_op'].items():assert ns==statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm)
    targets=[r for r in summary if not r['cell'].startswith('tranco')];risk=[r for r in summary if r['cell'].startswith('tranco')]
    accepted=all(r['optimization_gain_passed'] for r in targets) and not any(r['optimization_risk_failed'] for r in risk)
    decision=read(out/'decision.json')
    assert decision=={'BM_optimization_gate_passed':accepted,'target_failures':[r['cell'] for r in targets if not r['optimization_gain_passed']],
                      'risk_failures':[r['cell'] for r in risk if r['optimization_risk_failed']],
                      'BM_ART_numerical_target_met_all':all(r['BM_ART_numerical_target_met'] for r in summary),'production_adopted':False,'dynamic_throughput_measured':False}
    save(out,{'status':'verified','timing_rows':4320,'cells':6,'original_BM_used':True,**decision})

if __name__=='__main__':main()

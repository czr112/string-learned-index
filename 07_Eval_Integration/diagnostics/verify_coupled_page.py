"""Independent F5 publication, identity, semantic, space and paired-time audit."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
ARMS=['live','live_AA','D1','D1_AA']
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def records(path):return [json.loads(s) for s in path.read_text(encoding='utf-8').splitlines() if s.startswith('{')]
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
    for i in range(begin,len(source)):
        if source[i]=='{':depth+=1
        if source[i]=='}':
            depth-=1
            if not depth:return source[start:i+1]
    raise AssertionError('unclosed function')
def save(out,result):
    result.update(verifier_sha256=sha(Path(__file__)),commands_log_sha256=sha(out/'commands.log'))
    (out/'independent_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(out.name,json.dumps(result))

def main():
    p=argparse.ArgumentParser();p.add_argument('entry',type=Path);p.add_argument('--timing',type=Path);args=p.parse_args()
    out=args.entry.resolve();meta=identity(out);gates=read(out/'gates.json')
    assert gates['normal']==gates['sanitized'] and len(gates['normal'])==4
    assert records(out/'commands.log')==[r for build in gates.values() for rows in build.values() for r in rows]
    old=(ROOT/'diagnostics/dirty_compact_index.hpp').read_text(encoding='utf-8')
    new=(ROOT/'diagnostics/coupled_page_index.hpp').read_text(encoding='utf-8')
    retained=['bool mark_dirty(', 'void clear_dirty(', 'template<class P> static auto route(',
              'bool insert(', 'bool update(', 'bool erase(', 'std::vector<Row> scan(', 'void compact()']
    for signature in retained:assert function(old,signature)==function(new,signature),signature
    assert 'return root_->page(key)->find_single(key);' in function(new,'std::optional<std::uint64_t> find(')
    rebuild=function(new,'Pages::iterator rebuild(')
    assert rebuild.index('prepare_root(')<rebuild.index('clear_dirty(')<rebuild.index('root_.swap(next_root)')
    prepare=function(new,'std::unique_ptr<FlatRoot> prepare_root(')
    assert prepare.index('root_fault(0)')<prepare.index('make_unique<FlatRoot>')<prepare.index('root_fault(1)')
    verify=function(new,'std::size_t verify()')
    assert verify.index('root membership')<verify.index('core->verify()')
    f4=read(ROOT/'results/f4_route_attribution_20261018/identity.json')
    leaf='07_Eval_Integration/diagnostics/route_attribution_page.hpp'
    assert meta['sources'][leaf]==f4['sources'][leaf]
    total=0;space=[];work=[]
    for name,rows in gates['normal'].items():
        assert rows[-1]['status']=='passed';total+=rows[-1]['checks']
        if name=='synthetic':
            root=next(r for r in rows if r['phase']=='root_publication')
            assert root=={'phase':'root_publication','failure_scenarios':8,'binary_queries':65536,'inplace_root_publications':1,'final_pages':1}
            dirty=next(r for r in rows if r['phase']=='dirty_locality')
            assert dirty['failure_scenarios']==5 and dirty['clean_compact_targets']==0 and dirty['one_dirty_rebuilt_rows']==2047
            continue
        row=rows[0];assert row['rows']==meta['inputs'][name]['rows']
        assert row['root_prepared']==row['root_publications']==row['rebuilds']+1
        assert row['root_entries_copied']>=row['root_publications'] and row['root_bytes']>0
        assert row['compact_targets']<=row['dirty_before_compact'] and row['max_source_pages']<=2 and row['max_rebuild_rows']<=2049
        ratio=row['initial_slotted_bytes']/row['initial_d1_bytes']
        space.append({'cell':name,'live_over_D1':ratio,'space_gate_passed':ratio<=1.10})
        work.append({k:row[k] for k in ('root_publications','root_entries_copied','root_key_bytes_copied','largest_root_entries')})
    entry_decision=read(out/'entry_decision.json')
    assert entry_decision=={'space_cells':space,'timing_allowed':all(r['space_gate_passed'] for r in space)}
    assert total==sum(read(out/'verification.json')['checks_per_build'].values())
    save(out,{'status':'verified','checks_per_build':total,'builds':2,'root_failure_scenarios':8,
              'unchanged_F4_leaf':True,'unchanged_F4M_functions':retained,'root_work':work,'space_gate_passed':entry_decision['timing_allowed']})
    if args.timing is None:return
    entry=out;out=args.timing.resolve();timing=identity(out,True)
    assert timing['entry_identity_sha256']==sha(entry/'identity.json')
    raw=read(out/'raw.json');summary=read(out/'summary.json');gates=read(out/'gates.json');logged=records(out/'commands.log')
    assert len(raw)==960 and len(summary)==len(gates)==6
    assert [r for r in logged if r['phase']=='pass']==[{k:v for k,v in r.items() if k not in ('cell','process')} for r in raw]
    expected=[{k:v for k,v in g.items() if k!='cell'} for g in gates]
    assert [r for r in logged if r['phase']=='gate']==[g for g in expected for _ in range(2)]+[g for g in expected for _ in range(5)]
    failed=[];unresolved=[]
    for recorded in summary:
        cell=recorded['cell'];rows=[r for r in raw if r['cell']==cell];assert len(rows)==160
        gate=next(g for g in gates if g['cell']==cell)
        assert gate['inplace_updates']==gate['writes'] and gate['rebuilds']==0 and gate['root_publications']==1
        assert gate['root_entries_copied']==gate['pages']
        assert all(r['operations']==gate['queries'] and r['elapsed_ns']>0 for r in rows)
        pairs={n:[] for n in ('live/D1','live_AA/live','D1_AA/D1')}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==32
            table={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(table)==32
            for repeat in range(8):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(4))
                assert [r['arm'] for r in seq]==[ARMS[(x+repeat+process)%4] for x in (0,1,3,2)]
                assert len({r['checksum'] for r in seq})==1
            for name in pairs:
                a,b=name.split('/');pairs[name].append(statistics.median(table[r,a]/table[r,b] for r in range(8)))
        bands={a:max(max(x,1/x) for x in pairs[a+'_AA/'+a]) for a in ('live','D1')}
        assert recorded['aa_bands']==bands
        for name,points in pairs.items():
            a,b=name.split('/');band=max(bands[a.replace('_AA','')],bands[b])
            assert recorded['ratios'][name]=={'median':statistics.median(points),'process_medians':points,'slower':sum(x>1 for x in points),'band':band}
        r=recorded['ratios']['live/D1'];stop=r['median']>1.15 and r['slower']>=4 and r['median']>r['band']
        status='failed' if stop else 'within budget' if r['median']<=1.15 else 'unresolved'
        assert recorded['read_budget_failed']==stop and recorded['read_budget_status']==status
        for arm,value in recorded['ns_per_op'].items():
            assert value==statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm)
        if stop:failed.append(cell)
        if status=='unresolved':unresolved.append(cell)
    decision=read(out/'decision.json')
    assert decision['read_budget_failed_cells']==failed and decision['unresolved_cells']==unresolved
    assert decision['dynamic_gate_allowed']==(not failed and not unresolved)
    assert not decision['production_adopted'] and not decision['full_dynamic_matrix_started']
    save(out,{'status':'verified','timing_rows':960,'cells':6,'read_budget_failed_cells':failed,
              'unresolved_cells':unresolved,'dynamic_gate_allowed':decision['dynamic_gate_allowed']})

if __name__=='__main__':main()

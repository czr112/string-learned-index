"""Independent F3 identity/model-origin, semantic-record, and paired-time audit."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE=Path(__file__).resolve().parents[2]


def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def records(path):return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line.startswith('{')]


def identity(out,archive=False):
    meta=read(out/'identity.json')
    for path,digest in meta['sources'].items():
        assert sha(WORKSPACE/path)==digest,('source drift',path)
        if archive:assert sha(out/'frozen_sources'/path)==digest,('archive drift',path)
    for path,digest in meta['artifacts'].items():assert sha(out/path)==digest
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        if not archive:assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    return meta


def function(source,signature):
    start=source.index(signature);begin=source.index('{',start);depth=0
    for i in range(begin,len(source)):
        if source[i]=='{':depth+=1
        if source[i]=='}':
            depth-=1
            if not depth:return source[start:i+1]
    raise AssertionError('unclosed function')


def save(out,value):
    value.update(verifier_sha256=sha(Path(__file__)),commands_log_sha256=sha(out/'commands.log'))
    (out/'independent_verification.json').write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    print(out.name,json.dumps(value))


def main():
    parser=argparse.ArgumentParser();parser.add_argument('entry',type=Path);parser.add_argument('--timing',type=Path);args=parser.parse_args()
    entry=args.entry.resolve();meta=identity(entry,True);gates=read(entry/'gates.json');decision=read(entry/'entry_decision.json')
    assert gates['normal']==gates['sanitized'] and len(gates['normal'])==4
    assert records(entry/'commands.log')==[r for build in gates.values() for rows in build.values() for r in rows]
    core=(WORKSPACE/'07_Eval_Integration/diagnostics/slotted_page_core.hpp').read_text(encoding='utf-8')
    original=(WORKSPACE/'07_Eval_Integration/diagnostics/collision_refined_index.hpp').read_text(encoding='utf-8')
    signatures=['static Segment certificate(', 'void train(', 'Window model_window_x(',
                'std::uint32_t directory_block(const Region& r,std::string_view s,Work* work,',
                'std::optional<std::uint64_t> verify_range(', 'template<Lookup Policy> Location locate_blocks(',
                'std::size_t verify_numeric_certificate()']
    for signature in signatures:assert function(core,signature)==function(original,signature),signature
    total=0
    for name,rows in gates['normal'].items():
        assert rows[-1]['status']=='passed';total+=rows[-1]['checks']
        record=rows[0];assert record['max_source_pages']<=2 and record['max_rebuild_rows']<=2049
        if name=='synthetic':assert record['failed_rebuilds']==3 and record['coalesces']>0 and record['splits']>0
        else:
            assert record['rows']==meta['inputs'][name]['rows']
            cell=next(r for r in decision['space_cells'] if r['cell']==name)
            assert cell['slotted_over_D1_bytes']==record['initial_slotted_bytes']/record['initial_d1_bytes']
            assert cell['slotted_over_control_bytes']==record['initial_slotted_bytes']/record['initial_control_bytes']
            assert cell['space_gate_passed']==(cell['slotted_over_D1_bytes']<=1.10)
            assert record['inplace_updates']>=1024 and record['inplace_inserts']>0 and record['inplace_erases']>0
    assert decision['timing_allowed']==all(c['space_gate_passed'] for c in decision['space_cells'])
    assert total==sum(read(entry/'verification.json')['checks_per_build'].values())
    save(entry,{'status':'verified','checks_per_build':total,'builds':2,'gates_per_build':4,
                'unchanged_D1_functions':signatures,'space_gate_passed':decision['timing_allowed']})
    if args.timing is None:return
    out=args.timing.resolve();timing_meta=identity(out)
    assert timing_meta['entry_identity_sha256']==sha(entry/'identity.json')
    raw=read(out/'raw.json');summary=read(out/'summary.json');preflight=read(out/'gates.json');logged=records(out/'commands.log')
    assert len(raw)==1080 and len(summary)==len(preflight)==6
    assert [r for r in logged if r['phase']=='pass']==[{k:v for k,v in r.items() if k not in ('cell','process')} for r in raw]
    expected=[{k:v for k,v in g.items() if k!='cell'} for g in preflight]
    assert [r for r in logged if r['phase']=='gate']==[g for g in expected for _ in range(2)]+[g for g in expected for _ in range(5)]
    for g in preflight:assert g['inplace_updates']==g['writes'] and g['rebuilds']==0
    failed=[]
    for recorded in summary:
        cell=recorded['cell'];rows=[r for r in raw if r['cell']==cell];assert len(rows)==180
        gate=next(r for r in preflight if r['cell']==cell)
        assert all(r['operations']==gate['queries'] and r['elapsed_ns']>0 for r in rows)
        pairs={name:[] for name in recorded['ratios']}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==36
            t={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(t)==36
            for repeat in range(6):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(6))
                assert {r['arm'] for r in seq}=={'live','live_AA','control','control_AA','D1','D1_AA'}
                assert len({r['checksum'] for r in seq})==1
            for name in pairs:
                a,b=name.split('/');pairs[name].append(statistics.median(t[r,a]/t[r,b] for r in range(6)))
        bands={arm:max(max(x,1/x) for x in pairs[arm+'_AA/'+arm]) for arm in ('live','control','D1')}
        assert recorded['aa_bands']==bands
        for name,points in pairs.items():
            a,b=name.split('/');band=max(bands.get(a.replace('_AA',''),1),bands.get(b,1))
            r=recorded['ratios'][name];median=statistics.median(points)
            assert r['process_medians']==points and r['median']==median and r['band']==band
            assert r['slower']==sum(x>1 for x in points)
            assert r['clear_regression_5pct']==(median>1.05 and sum(x>1 for x in points)>=4 and median>band)
        for arm,value in recorded['ns_per_op'].items():
            assert value==statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm)
        r=recorded['ratios']['live/D1'];stop=r['median']>1.15 and r['slower']>=4 and r['median']>r['band']
        assert recorded['read_budget_failed']==stop
        if stop:failed.append(cell)
    decision=read(out/'decision.json')
    assert decision['read_budget_failed_cells']==failed and not decision['production_adopted'] and not decision['full_dynamic_matrix_started']
    save(out,{'status':'verified','timing_rows':len(raw),'cells':len(summary),'read_budget_failed_cells':failed})


if __name__=='__main__':main()

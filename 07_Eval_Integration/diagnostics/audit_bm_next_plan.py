"""Read-only evidence audit for the next plan; preserve all original measurements and decisions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WS=ROOT.parent
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def paired(rows,cell,x,y):
    selected=[r for r in rows if r.get('cell',r.get('label'))==cell]
    medians=[]
    for process in range(5):
        t={(r['repeat'],r['arm']):r['elapsed_ns'] for r in selected if r['process']==process}
        assert len(t)==24
        medians.append(statistics.median(t[k,x]/t[k,y] for k in range(6)))
    return {'process_medians':medians,'median':statistics.median(medians)}

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='bm_next_plan_audit_20261019');p.add_argument('--recheck-clean',action='store_true');a=p.parse_args()
    assert Path(a.output).name==a.output
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False)
    inputs={};result={}
    root_round=ROOT/'results/bm_root_chunk_20261019'
    root_meta=read(root_round/'identity.json')
    driver=ROOT/'diagnostics/bench_bm_root_chunk.cpp'
    rel=driver.relative_to(WS).as_posix()
    assert sha(driver)==root_meta['sources'][rel]
    assert sha(root_round/'build/rootchunk')==root_meta['artifacts']['build/rootchunk']
    source=driver.read_text(encoding='utf-8')
    assert 'const char* names[]={"flat","chunked","flat_AA","chunked_AA"};' in source
    assert 'const auto checksum=pass(arm>=2);' in source
    actual={'flat':'flat','chunked':'flat_AA','flat_AA':'chunked','chunked_AA':'chunked_AA'}
    raw=read(root_round/'raw.json');assert len(raw)==360
    root_rows=[]
    for cell in dict.fromkeys(r.get('cell',r.get('label')) for r in raw):
        ratio=paired(raw,cell,'flat_AA','flat')
        aa_flat=paired(raw,cell,'chunked','flat');aa_chunk=paired(raw,cell,'chunked_AA','flat_AA')
        band=max(max(v,1/v) for aa in (aa_flat,aa_chunk) for v in aa['process_medians'])
        root_rows.append({'cell':cell,'actual_chunked_over_flat':ratio,'actual_AA_band':band,
                          'registered_absolute_5_percent_gate':abs(ratio['median']-1)<=.05})
    result['root_labels']={'frozen_source_and_binary_match':True,'reported_label_to_actual_arm':actual,
        'rows':root_rows,'original_timing_gate_supported':False,
        'publication_copy_work':'arithmetic estimate; no real copy-on-write publication or allocator measurement',
        'action':'withdraw original permission to integrate; preserve raw data; requalify any new root design before integration'}
    inputs['root_identity']=sha(root_round/'identity.json');inputs['root_raw']=sha(root_round/'raw.json');inputs['root_source']=sha(driver)
    dynamic_round=ROOT/'results/bm_dynamic_read_20261019';dynamic_meta=read(dynamic_round/'identity.json')
    dynamic_driver=ROOT/'diagnostics/bench_bm_dynamic_read.cpp';dynamic_runner=ROOT/'diagnostics/run_bm_dynamic_read.py'
    for x in (dynamic_driver,dynamic_runner):assert sha(x)==dynamic_meta['sources'][x.relative_to(WS).as_posix()]
    assert 'const char* names[]={"dynamic","dynamic_AA","snapshot","snapshot_AA"};' in dynamic_driver.read_text(encoding='utf-8')
    assert 'const auto& index=(arm<2)?dynamic:snapshot;' in dynamic_driver.read_text(encoding='utf-8')
    raw=read(dynamic_round/'raw.json');assert len(raw)==480
    result['dynamic_read_direction']={'direction':'elapsed(dynamic)/elapsed(snapshot)',
        'rows':[{'cell':c,**paired(raw,c,'dynamic','snapshot')} for c in dict.fromkeys(r['cell'] for r in raw)],
        'claim':'observed median budget only; not equivalence, mixed throughput, miss-heavy coverage or pure-write latency'}
    assert all(x['median']<=1.15 for x in result['dynamic_read_direction']['rows'])
    result['dynamic_read_direction']['within_observed_1_15_budget']=True
    result['dynamic_read_direction']['coverage']={'correctness_traces':4,'sanitized_cells':2,'timed_queries':'all present final keys; absent probes outside timing'}
    inputs['dynamic_identity']=sha(dynamic_round/'identity.json');inputs['dynamic_raw']=sha(dynamic_round/'raw.json')
    old=ROOT/'results/bm_clean_rebuild_20261019/comparison.json';comparison=read(old)
    result['old_clean_comparison']={'reported_checksums_as_decimal_strings':[str(x) for x in comparison['fields']['checksum']],
        'old_rebuilt_stdout_preserved':False,'action':'compare original stdout bytes using exact integers; never use IEEE-754 Number for uint64 equality'}
    inputs['old_clean_comparison']=sha(old)
    if a.recheck_clean:
        baseline=ROOT/'results/bm_space_20261019b';meta=read(baseline/'identity.json')
        binary=WS/'builds/rebuild_20261019/clean'
        assert sha(binary)==meta['artifacts']['build/clean']==comparison['recorded_sha256']
        x=meta['inputs']['english_words']
        for key in ('rows','queries'):assert sha(WS/x[key+'_path'])==x[key+'_sha256']
        records=[r for r in read(baseline/'capacity.json') if r['arm']=='bm_base' and r['cell']=='english_words']
        assert len(records)==3 and len({r['checksum'] for r in records})==1
        expected=records[0]
        deterministic=['arm','rows','queries','hits','checksum','semantic_checks','input_heap_bytes','output_heap_bytes','retains_input','self_report']
        observed=[]
        for repeat in range(2):
            cmd=['taskset','-c',str(min(os.sched_getaffinity(0))),str(binary),'bm_base',str(WS/x['rows_path']),str(WS/x['queries_path'])]
            run=subprocess.run(cmd,capture_output=True,timeout=120)
            (out/f'clean_{repeat}.stdout').write_bytes(run.stdout);(out/f'clean_{repeat}.stderr').write_bytes(run.stderr)
            assert run.returncode==0
            r=json.loads(run.stdout.splitlines()[-1]);equal={k:r[k]==expected[k] for k in deterministic}
            observed.append({'command':cmd,'returncode':run.returncode,'checksum_decimal':str(r['checksum']),
                             'deterministic_fields_equal':equal,'stdout_sha256':sha(out/f'clean_{repeat}.stdout')})
            assert all(equal.values()),equal
        result['clean_exact_recheck']={'binary_sha256':sha(binary),'recorded_checksum_decimal':str(expected['checksum']),
            'processes':observed,'all_deterministic_fields_match':True,
            'scope':'two runs of previously rebuilt clean binary, one cell; libraries and full experiment matrix not rebuilt',
            'old_difference_cause':'old raw rebuilt stdout unavailable; V8 Number round-trip can reproduce the recorded rounded value, consistent with a comparison-chain precision error'}
        inputs['baseline_identity']=sha(baseline/'identity.json');inputs['baseline_capacity']=sha(baseline/'capacity.json')
    save(out/'audit.json',result)
    save(out/'identity.json',{'audit_script_sha256':sha(Path(__file__)),'inputs_sha256':inputs,
        'derived_audit_sha256':sha(out/'audit.json'),'new_performance_measurement':False,'model_changed':False})
    print(json.dumps(result,ensure_ascii=False))

if __name__=='__main__':main()

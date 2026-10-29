"""AA-only protocol calibration, followed by one frozen historical confirmation."""
from pathlib import Path
import argparse
import json
import math
import os
import statistics
import hashlib
from run_bm_tx import Run,ROOT,DIAG,save,sha

PROTOCOL=ROOT/'docs/PROTOCOL_BM_QUERY_STABILITY_20261030.md'
HISTORICAL=ROOT/'builds/bm_tx_query_bm1_20261030/cases.json'
PILOT=('words_95_5_uniform_growth','urls_80_20_hotspot_replacement')
STATES=('bm_dynamic','candidate_dynamic','bm_snapshot','candidate_snapshot')

def interval(values):
    n=len(values);critical={4:3.1824463053,16:2.1314495456}[n]
    mean=statistics.fmean(values);half=critical*statistics.stdev(values)/math.sqrt(n)
    return {'n':n,'df':n-1,'ratio':math.exp(mean),'ci95':[math.exp(mean-half),math.exp(mean+half)]}

def analyze(blocks,contrasts=False):
    groups={}
    for block in blocks:
        rows=block['measurements']
        if len(rows)!=32 or len({(r['state'],r['round']) for r in rows})!=32:raise ValueError('incomplete batch records')
        groups[block['process']]={s:sorted((r for r in rows if r['state']==s),key=lambda r:r['round']) for s in STATES}
    if len(groups)!=len(blocks):raise ValueError('duplicate process')
    answer={'aa':{},'diagnostics':{}}
    for state in STATES:
        checks={}
        for name,predicate in (('parity',lambda r:r['round']%2==1),('halves',lambda r:r['round']>=4)):
            values=[]
            for process in groups.values():
                rows=process[state]
                means=[statistics.fmean(math.log(r['ns']/r['lookups']) for r in rows if predicate(r)==side) for side in (False,True)]
                values.append(means[1]-means[0])
            result=interval(values);result['pass']=result['ci95'][0]>=1/1.05 and result['ci95'][1]<=1.05;checks[name]=result
        answer['aa'][state]={'checks':checks,'pass':all(v['pass'] for v in checks.values())}
        rows=[r for p in groups.values() for r in p[state]]
        answer['diagnostics'][state]={'wall_cpu_ratio_range':[min(r['ns']/r['cpu_ns'] for r in rows),max(r['ns']/r['cpu_ns'] for r in rows)],
             'involuntary_switches':sum(r['involuntary'] for r in rows),'voluntary_switches':sum(r['voluntary'] for r in rows),
             'minor_faults':sum(r['minor_faults'] for r in rows),'major_faults':sum(r['major_faults'] for r in rows),
             'migrations':sum(r['cpu_before']!=r['cpu_after'] for r in rows)}
    answer['all_aa_pass']=all(v['pass'] for v in answer['aa'].values())
    if contrasts:
        answer['contrasts']={}
        for name,candidate,control,limit in (
            ('candidate_vs_bm1_dynamic','candidate_dynamic','bm_dynamic',1.05),
            ('candidate_vs_bm1_snapshot','candidate_snapshot','bm_snapshot',1.05),
            ('candidate_dynamic_vs_snapshot','candidate_dynamic','candidate_snapshot',1.15),
            ('bm1_dynamic_vs_snapshot','bm_dynamic','bm_snapshot',1.15)):
            values=[]
            for process in groups.values():
                means=[statistics.fmean(math.log(r['ns']/r['lookups']) for r in process[s]) for s in (candidate,control)]
                values.append(means[0]-means[1])
            result=interval(values);quality=answer['aa'][candidate]['pass'] and answer['aa'][control]['pass']
            verdict='pass' if result['ci95'][1]<=limit else ('regression' if result['ci95'][0]>limit else 'undecided')
            result.update(limit=limit,aa_quality_pass=quality,performance_interval_verdict=verdict,
                          verdict=verdict if quality else 'measurement_unresolved')
            answer['contrasts'][name]=result
    return answer

def validated_selection(path,closure_sha,input_hashes):
    selection=json.loads(path.read_text());directory=path.resolve().parent
    report_path=directory/'report.json';report=json.loads(report_path.read_text())
    if (report.get('status')!='complete' or report.get('phase')!='calibrate'
            or sha(report_path)!=selection['calibration_report_sha256']
            or selection['protocol_sha256']!=sha(PROTOCOL)
            or selection['runner_sha256']!=sha(Path(__file__))
            or selection['source_closure_sha256']!=closure_sha
            or json.loads((directory/'input_hashes.json').read_text())!=input_hashes):
        raise ValueError('incomplete or floating calibration selection')
    if set(report['results'])!={'250','1000'}:raise ValueError('calibration configurations incomplete')
    passing=[]
    for floor in (250,1000):
        results=report['results'][str(floor)]
        if set(results)!=set(PILOT):raise ValueError('calibration cells incomplete')
        for cell in PILOT:
            blocks=[json.loads((directory/f'{floor}_{cell}_p{p:02d}.json').read_text()) for p in range(4)]
            if analyze(blocks)!=results[cell]:raise ValueError('calibration analysis drift')
        if all(v['all_aa_pass'] for v in results.values()):passing.append(floor)
    chosen=min(passing) if passing else None
    if chosen is None or selection['selected_ms']!=chosen or report['selection']['selected_ms']!=chosen:
        raise ValueError('calibration did not select this configuration')
    return selection

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--phase',choices=('calibrate','confirm'),default='calibrate')
    parser.add_argument('--selection',type=Path)
    args=parser.parse_args();run=Run(args.output)
    run.sources.update({str(Path(__file__).resolve()):sha(Path(__file__)),str(PROTOCOL):sha(PROTOCOL)})
    try:
        cases=json.loads(HISTORICAL.read_text());inputs={}
        for name in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025'):
            inputs.update(json.loads((ROOT/'07_Eval_Integration/results'/name/'manifest.json').read_text()))
        historical=json.loads((ROOT/'07_Eval_Integration/results/bm_query_recheck_20261024/cases.json').read_text())
        if len(cases)!=32 or set(cases)!=set(inputs):raise ValueError('cell coverage')
        input_hashes={}
        for cell,meta in cases.items():
            paths=list(map(Path,meta['paths']));expected=(inputs[cell]['rows_sha256'],inputs[cell]['operations_sha256'],None,historical[cell]['final_sha256'],historical[cell]['query_sha256'])
            for p,h in zip(paths,expected):
                actual=sha(p)
                if h is not None and actual!=h:raise ValueError('historical input drift')
                input_hashes[str(p)]=actual
        save(run.output/'input_hashes.json',input_hashes)
        cpu=min(os.sched_getaffinity(0));save(run.output/'environment.json',{
            'affinity':sorted(os.sched_getaffinity(0)),'cpu':cpu,'uname':list(os.uname()),
            'cpuinfo':Path('/proc/cpuinfo').read_text(),'loadavg':Path('/proc/loadavg').read_text()})
        definitions={'BM_TX_CONTROL_BM1':'1','BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp')}
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        binary=run.build('stability',[DIAG/'bm_query_stability.cpp',model],definitions=definitions)
        closure_sha=hashlib.sha256(json.dumps(run.sources,sort_keys=True).encode()).hexdigest()
        registration={'phase':args.phase,'processes':4 if args.phase=='calibrate' else 16,'rounds':8,
                      'batch_floors_ms':[250,1000],'cells':list(PILOT) if args.phase=='calibrate' else sorted(cases),
                      'protocol_sha256':sha(PROTOCOL),'runner_sha256':sha(Path(__file__)),'cpu':cpu}
        if args.phase=='confirm':
            if args.selection is None:raise ValueError('selection required')
            selection=validated_selection(args.selection,closure_sha,input_hashes)
            floors=[selection['selected_ms']];save(run.output/'selection.json',selection)
        else:floors=[250,1000]
        registration['batch_floors_ms']=floors
        save(run.output/'registration.json',registration);run.report.update(phase=args.phase,results={})
        for floor in floors:
            results={}
            for cell in registration['cells']:
                blocks=[]
                for process in range(registration['processes']):
                    block=json.loads(run.execute(['taskset','-c',cpu,binary,'run',*cases[cell]['paths'],process,floor],f'{floor}_{cell}_p{process:02d}.json'))
                    blocks.append(block)
                results[cell]=analyze(blocks,args.phase=='confirm')
                run.report['results'][str(floor)]=results;save(run.output/'report.json',run.report)
                print(f"{floor}ms {cell}: AA={'pass' if results[cell]['all_aa_pass'] else 'unresolved'}",flush=True)
        if args.phase=='calibrate':
            passing=[int(k) for k,v in run.report['results'].items() if all(x['all_aa_pass'] for x in v.values())]
            selection={'selected_ms':min(passing) if passing else None,'protocol_sha256':sha(PROTOCOL),
                       'runner_sha256':sha(Path(__file__)),'source_closure_sha256':closure_sha,
                       'decision_basis':'AA only; no performance contrasts calculated'}
            run.report['selection']=selection
        if any(sha(p)!=h for p,h in input_hashes.items()):raise ValueError('input drift at completion')
        run.finish()
        if args.phase=='calibrate':
            selection=dict(run.report['selection'],calibration_report_sha256=sha(run.output/'report.json'))
            save(run.output/'selection.json',selection)
        print(json.dumps({'status':'complete','phase':args.phase,'selection':run.report.get('selection')}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

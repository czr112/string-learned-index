"""Preregistered 16-process confirmation; 4-process runs are discovery only.

Eight paired fresh-state repetitions/process. All observations retained. Metrics
aggregate log times within process before t CI across processes (df=n-1). A/A CI
must independently fit [1/1.05,1.05] for each compared arm and metric. Primary:
candidate/BM1 write <=0.85; throughput >=1.10; find <=1.05. Per-cell intervals
are descriptive pointwise intervals, not a simultaneous universal adoption claim.
Terminal query is a NEW fixed-seed live-map mixed90 probe set; it does not replace
the previous exact hot/cold suite. ART is included only in the separate point run.
"""
from __future__ import annotations
import argparse
import json
import math
import os
import shlex
from pathlib import Path
import statistics
from run_bm_tx import ROOT,DIAG,PINS,Run,sha,save

def interval(values):
    n=len(values);critical={4:3.1824463053,16:2.1314495456}[n]
    center=statistics.fmean(values);half=critical*statistics.stdev(values)/math.sqrt(n)
    return {'n':n,'ratio':math.exp(center),'ci95':[math.exp(center-half),math.exp(center+half)]}

def summarize(records):
    mixed={};terminal={}
    for row in records:
        destination=mixed if row['channel']=='mixed' else terminal
        destination.setdefault(row['process'],{}).setdefault(row['arm'],[]).append(row)
    answer={}
    for channel,groups,metrics in [('mixed',mixed,('write','find','pure')),('terminal',terminal,('find',))]:
        summary={}
        arms=list(next(iter(groups.values())))
        for control in [x for x in arms if x!='tx']:
            summary[control]={}
            for metric in metrics:
                def aggregate(process,arm,key):
                    return statistics.fmean(math.log(r[key]) for r in process[arm])
                paired=[aggregate(p,'tx',metric)-aggregate(p,control,metric) for p in groups.values()]
                result=interval(paired);quality={}
                for arm in ('tx',control):
                    quality[arm]=interval([aggregate(p,arm,metric+'_aa')-aggregate(p,arm,metric) for p in groups.values()])
                aa=all(v['ci95'][0]>=1/1.05 and v['ci95'][1]<=1.05 for v in quality.values())
                limit=1/1.10 if metric=='pure' else (0.85 if metric=='write' else 1.05)
                if result['ci95'][1]<=limit:verdict='pass'
                elif result['ci95'][0]>1.05:verdict='regression'
                elif result['ci95'][0]>limit:verdict='target_not_met'
                else:verdict='undecided'
                result.update(limit=limit,aa=quality,aa_quality_pass=aa,performance_interval_verdict=verdict,
                              verdict=verdict if aa else 'measurement_unresolved')
                summary[control][metric]=result
        answer[channel]=summary
    return answer

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--processes',type=int,choices=(4,16),default=16)
    parser.add_argument('--cells',default='')
    parser.add_argument('--point-only',action='store_true')
    args=parser.parse_args();run=Run(args.output)
    run.sources[str(Path(__file__).resolve())]=sha(Path(__file__))
    protocol=ROOT/'docs/PROTOCOL_BM_TX_20261030.md';run.sources[str(protocol)]=sha(protocol)
    try:
        vendor=ROOT/'02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'
        run.includes.extend([ROOT/'07_Eval_Integration/adapters/our_index',ROOT/'07_Eval_Integration/adapters/art',vendor])
        run.flags.extend(f'-I{x}' for x in run.includes[-3:])
        art_object=run.output/'art.o';partial=run.output/'art.partial'
        c_flags=['-std=c11','-DNDEBUG','-O3','-march=x86-64-v3','-I'+str(vendor)]
        dependency=run.execute(['gcc',*c_flags,'-MM',vendor/'art.c'],'art_dependencies.log')
        c_paths=[(ROOT/Path(p)).resolve() for p in shlex.split(dependency.replace('\\\n',' ').split(':',1)[1])]
        c_hashes={str(p):sha(p) for p in c_paths};run.sources.update(c_hashes)
        run.execute(['gcc',*c_flags,'-c',vendor/'art.c','-o',partial],'art_build.log')
        if any(sha(p)!=h for p,h in c_hashes.items()):raise RuntimeError('ART source drift during compile')
        os.replace(partial,art_object);run.binaries[str(art_object)]=sha(art_object)
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src'
        sources=[DIAG/'bench_bm_tx.cpp',*[model/p for p in ('block_model.cpp','residual_tags.cpp','query_cost.cpp','region_planner.cpp','learned_index.cpp','benchmark/model_factories.cpp')],
                 ROOT/'07_Eval_Integration/adapters/our_index/our_index_adapter.cpp',ROOT/'07_Eval_Integration/adapters/art/art_adapter.cpp',
                 ROOT/'07_Eval_Integration/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp']
        binary=run.build('bench',sources,objects=[art_object])
        cpu=min(os.sched_getaffinity(0));run.report.update(processes=args.processes,discovery_only=args.processes!=16,
                                                         point_only=args.point_only,cpu=cpu,cells={})
        manifests=[ROOT/'07_Eval_Integration/results'/d/'manifest.json' for d in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025')]
        entries={}
        for p in manifests:entries.update(json.loads(p.read_text()))
        selected=args.cells.split(',') if args.cells else sorted(entries)
        save(run.output/'registration.json',{'processes':args.processes,'repeats':8,'cells':selected,'point_only':args.point_only,
                                           'protocol_sha256':sha(protocol),'runner_sha256':sha(Path(__file__)),'candidate':'make_bm_tx_experiment_index',
                                           'terminal_probe':'mixed90_live_1800_synthetic_absent_200_seed_20261030'})
        for cell in selected:
            meta=entries[cell];rows=ROOT/meta['rows_path'];ops=ROOT/meta['operations_path']
            if sha(rows)!=meta['rows_sha256'] or sha(ops)!=meta['operations_sha256']:raise RuntimeError('trace drift')
            records=[]
            for process in range(args.processes):
                log=run.execute(['taskset','-c',cpu,binary,rows,ops,process,'point' if args.point_only else 'mixed'],f'{cell}_p{process:02d}.jsonl')
                records.extend(json.loads(x) for x in log.splitlines() if x.strip())
            run.report['cells'][cell]=summarize(records);save(run.output/'report.json',run.report)
            print(cell+': '+json.dumps({k:v['verdict'] for k,v in run.report['cells'][cell]['mixed']['bm1'].items()}),flush=True)
        run.finish();print('performance complete: '+str(run.output),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

"""Frozen TX directory cost bound: semantic coverage first, fixed cost pilot second."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import statistics
import subprocess
from run_bm_tx import Run,ROOT,DIAG,save,sha

PROTOCOL=ROOT/'docs/PROTOCOL_BM_TX_DIRECTORY_COST_20261030.md'
BASE='10e0494'
PILOT=tuple(f'{dataset}_80_20_{locality}_growth' for dataset in ('words','urls') for locality in ('uniform','hotspot'))
ARMS=('original','instrumented','original_AA','instrumented_AA')

def replace_once(text,before,after):
    if text.count(before)!=1:raise ValueError('unexpected diagnostic preimage: '+before[:60])
    return text.replace(before,after,1)

def generated_headers(run):
    destination=run.output/'diagnostic_sources';destination.mkdir()
    names={'bm_n1_page_core.hpp':'bm_dir_page_core.hpp','bm_n1_page_root.hpp':'bm_dir_page_root.hpp',
           'bm_n1_index.hpp':'bm_dir_index.hpp','bm_tx_index.hpp':'bm_dir_tx_index.hpp',
           'bm_followup_memory.hpp':'bm_directory_memory.hpp'}
    identity={}
    for original,generated in names.items():
        source=DIAG/original;relative=source.relative_to(ROOT).as_posix()
        committed=subprocess.check_output(['git','-C',str(ROOT),'show',BASE+':'+relative])
        if source.read_bytes()!=committed:raise ValueError('frozen control changed: '+relative)
        text=committed.decode()
        if original=='bm_followup_memory.hpp':
            text=replace_once(text,'epoch=0,requests=0,calls=0,throw_at=0;',
                             'epoch=0,requests=0,calls=0,throw_at=0,requested_bytes=0;')
            text=replace_once(text,'++epoch;requests=0;peak=0;tracking=true;',
                             '++epoch;requests=0;requested_bytes=0;peak=0;tracking=true;')
            text=replace_once(text,'++requests;','++requests;requested_bytes+=n;')
        else:
            for old,new in names.items():text=text.replace(old,new)
            for old,new in (('bm_n1_leaf','bm_dir_leaf'),('bm_n1_page','bm_dir_page'),('bm_tx_page','bm_dir_tx')):
                text=text.replace(old,new)
            if original=='bm_n1_page_core.hpp':
                text='#include "bm_directory_metrics.hpp"'+chr(10)+text
                text=replace_once(text,'if(block.count==16) {',
                    'if(block.count==16) {'+chr(10)+'            bm_directory_metrics::Timer directory_timer(blocks_.size(),sizeof(Block),sizeof(Fence));')
        output=destination/generated;output.write_bytes(text.encode())
        identity[original]={'revision':BASE,'preimage_sha256':hashlib.sha256(committed).hexdigest(),
                            'generated':str(output),'generated_sha256':sha(output)}
    save(run.output/'generation.json',identity)
    return {'BM_DIRECTORY_INDEX_HEADER':str(destination/'bm_dir_tx_index.hpp'),
            'BM_DIRECTORY_MEMORY_HEADER':str(destination/'bm_directory_memory.hpp')}

def interval(values,logarithmic=True):
    if len(values)!=4:raise ValueError('fixed four-process pilot')
    mean=statistics.fmean(values);half=3.1824463053*statistics.stdev(values)/2
    result={'n':4,'df':3,'center':mean,'ci95':[mean-half,mean+half]}
    if logarithmic:result={'n':4,'df':3,'ratio':math.exp(mean),'ci95':[math.exp(mean-half),math.exp(mean+half)]}
    return result

def analyze(blocks):
    if len(blocks)!=4:raise ValueError('missing process block')
    data={}
    for process,rows in enumerate(blocks):
        if len(rows)!=8 or {(r['arm'],r['round']) for r in rows}!={(a,n) for a in ARMS for n in range(2)}:
            raise ValueError('incomplete pilot records')
        if any(r['process']!=process or r['replays']!=8 for r in rows):raise ValueError('block identity')
        if len({r['checksum'] for r in rows})!=1:raise ValueError('pilot checksum')
        data[process]={arm:[r for r in rows if r['arm']==arm] for arm in ARMS}
    aa={}
    for arm in ('original','instrumented'):
        values=[statistics.fmean(math.log(r['write_ns']) for r in p[arm+'_AA'])-
                statistics.fmean(math.log(r['write_ns']) for r in p[arm]) for p in data.values()]
        result=interval(values);result['pass']=result['ci95'][0]>=1/1.05 and result['ci95'][1]<=1.05;aa[arm]=result
    overhead=interval([statistics.fmean(math.log(r['write_ns']) for arm in ('instrumented','instrumented_AA') for r in p[arm])-
                       statistics.fmean(math.log(r['write_ns']) for arm in ('original','original_AA') for r in p[arm]) for p in data.values()])
    overhead['pass']=overhead['ci95'][0]>=1/1.05 and overhead['ci95'][1]<=1.05
    share=interval([sum(r['directory_ns'] for arm in ('instrumented','instrumented_AA') for r in p[arm])/
                    sum(r['write_ns'] for arm in ('instrumented','instrumented_AA') for r in p[arm]) for p in data.values()],False)
    quality=all(v['pass'] for v in aa.values()) and overhead['pass']
    return {'aa':aa,'instrumentation':overhead,'branch_share':share,'quality_pass':quality,
            'candidate_headroom_pass':quality and share['ci95'][0]>=0.30}

def deterministic(rows):
    return [{k:v for k,v in row.items() if not k.endswith('_ns')} for row in rows]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();run=Run(args.output)
    run.sources.update({str(Path(__file__).resolve()):sha(Path(__file__)),str(PROTOCOL):sha(PROTOCOL)})
    try:
        definitions=generated_headers(run)
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        binary=run.build('directory_cost',[DIAG/'bm_tx_directory_cost.cpp',model],definitions=definitions)
        sanitizer=run.build('directory_cost_san',[DIAG/'bm_tx_directory_cost.cpp',model],True,definitions)
        entries={}
        for name in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025'):
            entries.update(json.loads((ROOT/'07_Eval_Integration/results'/name/'manifest.json').read_text()))
        historical=json.loads((ROOT/'07_Eval_Integration/results/bm_query_recheck_20261024/cases.json').read_text())
        cases=json.loads((ROOT/'builds/bm_tx_query_bm1_20261030/cases.json').read_text())
        if len(entries)!=32 or set(cases)!=set(entries):raise ValueError('cell coverage')
        paths={};hashes={}
        for cell,meta in entries.items():
            values=[ROOT/meta['rows_path'],ROOT/meta['operations_path'],Path(cases[cell]['paths'][3])]
            expected=[meta['rows_sha256'],meta['operations_sha256'],historical[cell]['final_sha256']]
            for p,h in zip(values,expected):
                if sha(p)!=h:raise ValueError('per-cell input identity')
                hashes[str(p)]=h
            paths[cell]=list(map(str,values))
        save(run.output/'input_hashes.json',hashes);save(run.output/'cases.json',paths)
        save(run.output/'registration.json',{'base':BASE,'pilot_cells':PILOT,'processes':4,'replays':8,'rounds':2,
             'protocol_sha256':sha(PROTOCOL),'runner_sha256':sha(Path(__file__)),'headroom_minimum':0.30,'required_qualified_cells':2})
        cpu=min(os.sched_getaffinity(0));run.report.update(semantic={},pilot={})
        for name,executable in (('opt',binary),('sanitizer',sanitizer)):
            results={}
            for cell,inputs in sorted(paths.items()):
                raw=run.execute([executable,'check',*inputs,0],f'{name}_{cell}.jsonl',
                                dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
                rows=[json.loads(line) for line in raw.splitlines()]
                if len(rows)!=2 or {r['arm'] for r in rows}!={'original','instrumented'}:raise ValueError('semantic output')
                results[cell]=rows
            run.report['semantic'][name]=results;save(run.output/'report.json',run.report)
            print(name+': semantic32 complete',flush=True)
        for cell in paths:
            if deterministic(run.report['semantic']['opt'][cell])!=deterministic(run.report['semantic']['sanitizer'][cell]):
                raise ValueError('non-deterministic semantic fields')
        for cell in PILOT:
            blocks=[]
            for process in range(4):
                raw=run.execute(['taskset','-c',cpu,binary,'bench',*paths[cell],process],f'pilot_{cell}_p{process:02d}.jsonl')
                blocks.append([json.loads(line) for line in raw.splitlines()])
            run.report['pilot'][cell]=analyze(blocks);save(run.output/'report.json',run.report)
            value=run.report['pilot'][cell]
            print(f"{cell}: branch_share={value['branch_share']['center']:.3f} quality={value['quality_pass']}",flush=True)
        run.report['candidate_eligible']=sum(v['candidate_headroom_pass'] for v in run.report['pilot'].values())>=2
        if any(sha(p)!=h for p,h in hashes.items()):raise ValueError('input drift at completion')
        run.finish();print(json.dumps({'status':'complete','candidate_eligible':run.report['candidate_eligible']}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

"""A-stage bounded 2x2 lookup attribution, actual adapter and same-object A/A."""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
UPSTREAM=WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'
ARMS=['old_plain','old_checked','new_plain','new_checked','new_AA','actual']
PAIRS=[('old_checked','old_plain'),('new_checked','new_plain'),('new_plain','old_plain'),
       ('new_checked','old_checked'),('actual','new_checked'),('actual','old_plain'),('new_AA','new_checked')]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,data):p.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n')


def summary(raw):
    result=[]
    for corpus in ['skew','tranco','mixed']:
        rows=[r for r in raw if r['corpus']==corpus]
        if len(rows)!=180:continue
        ratios={}
        for a,b in PAIRS:
            process_medians=[]
            for process in range(5):
                times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                process_medians.append(statistics.median(times[rep,a]/times[rep,b] for rep in range(6)))
            ratios[a+'/'+b]={'process_medians':process_medians,'median':statistics.median(process_medians),
                'faster':sum(x<1 for x in process_medians),'slower':sum(x>1 for x in process_medians)}
        band=max(max(x,1/x) for x in ratios['new_AA/new_checked']['process_medians'])
        for p in ratios.values():
            p['clear_slowdown']=p['median']>1.05 and p['slower']>=4 and p['median']>band
            p['clear_gain']=p['median']<.95 and p['faster']>=4 and 1/p['median']>band
        result.append({'corpus':corpus,'ratios':ratios,'aa_observed_band':band,
            'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}})
    return result


def verify_process(rows):
    assert len(rows)==36 and len({r['checksum'] for r in rows})==1
    assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in rows)
    counts=collections.Counter()
    for rep in range(6):
        seq=sorted([r for r in rows if r['repeat']==rep],key=lambda r:r['position'])
        assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==set(ARMS)
        for a,b in zip(seq,seq[1:]):counts[a['arm'],b['arm']]+=1
    assert len(counts)==30 and set(counts.values())=={1}
    for a in ARMS:assert sorted(r['position'] for r in rows if r['arm']==a)==list(range(6))


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='integration_attribution_20261018');args=parser.parse_args()
    assert Path(args.output).name==args.output
    out=ROOT/'results'/args.output
    inspection=json.loads((out/'inspection.json').read_text())
    assert all(sha(WORKSPACE/p)==h for p,h in inspection['frozen_sources'].items())
    timing=out/'timing';timing.mkdir(exist_ok=False);build=out/'build';build.mkdir(exist_ok=False)
    prior=ROOT/'results/static_interface_20261012c';old=json.loads((prior/'identity.json').read_text())
    log=(timing/'commands.log').open('x')
    meta={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          'cpu':min(os.sched_getaffinity(0)),'commands':[], 'sources':{}, 'inputs':{},
          'protocol':'5 processes x 6 even Williams rows x 200k full finds; immediate per-arm warm-up; shared old/new core within wrapper pairs; NC/AA same object',
          'interpretation':'checked/plain isolates check-associated codegen on identical core data; actual/new_checked still bundles ownership, allocation and separate-TU/code layout',
          'scope':'attribution only; no ART/HOT ranking, no production/default change; unchanged source/incoming map'}
    def run(command):
        meta['commands'].append(list(map(str,command)));log.write('$ '+' '.join(map(str,command))+'\n');log.flush()
        r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=900)
        log.write(r.stdout);log.write(r.stderr);log.flush()
        if r.returncode:raise RuntimeError('command failed: '+str(command)+'\n'+r.stderr[-5000:])
        return r
    meta['compiler']=run(['g++','--version']).stdout.splitlines()[0]
    assert meta['compiler']==old['compiler']
    desc=old['descriptor']
    common=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Iadapters/our_static',
        '-DSTATIC_CORE_SHA256="'+desc['core_sha256']+'"','-DSTATIC_ADAPTER_SHA256="'+desc['adapter_digest']+'"']
    files=[Path(__file__),ROOT/'diagnostics/inspect_static_integration.py',ROOT/'diagnostics/verify_integration_attribution.py',
           ROOT/'diagnostics/bench_integration_attribution.cpp',ROOT/'diagnostics/INTEGRATION_ATTRIBUTION_SPEC_20261018.md']
    objects=[]
    for name,path in [('matrix','diagnostics/bench_integration_attribution.cpp'),
                      ('adapter','adapters/our_static/our_static_adapter.cpp'),('input','learned-index/src/unified/runner.cpp')]:
        obj=build/(name+'.o');dep=build/(name+'.d')
        run(['g++','-O3']+common+['-MMD','-MF',str(dep),'-c',path,'-o',str(obj)])
        objects.append(str(obj))
        dependencies=dep.read_text().replace('\\\n',' ').split(':',1)[1].split()
        files.extend((ROOT/p).resolve() for p in dependencies)
    bench=build/'bench'
    run(['g++','-O3','-march=x86-64-v3']+objects+['-o',str(bench)])
    meta['sources']={str(p.relative_to(WORKSPACE)):sha(p) for p in sorted(set(files))}
    meta['binary_sha256']=sha(bench)
    (timing/'symbols.txt').write_text(run(['nm','-S','-C',str(bench)]).stdout)
    save(timing/'identity.json',meta)
    gates,raw=[],[]
    for corpus in ['skew','tranco','mixed']:
        cell=corpus+'_full_100000';inp=old['inputs'][cell]
        rows=WORKSPACE/inp['rows_path'];queries=WORKSPACE/inp['queries_path']
        assert sha(rows)==inp['rows_sha256'] and sha(queries)==inp['queries']['sha256']
        meta['inputs'][corpus]={'rows_path':inp['rows_path'],'queries_path':inp['queries_path'],
            'rows_sha256':sha(rows),'queries_sha256':sha(queries)}
        gate=run(['taskset','-c',str(meta['cpu']),str(bench),str(rows),str(queries),'-1']);assert not gate.stderr
        records=[json.loads(line) for line in gate.stdout.splitlines()];assert len(records)==2
        assert records[0]['phase']=='work' and records[1]['phase']=='gate' and records[1]['core_memory_equal']
        gates.append({'corpus':corpus,'work':records[0],'gate':records[1]});save(timing/'gates.json',gates)
        print('work',corpus,'different queries',records[0]['different_queries'],flush=True)
        for process in range(5):
            r=run(['taskset','-c',str(meta['cpu']),str(bench),str(rows),str(queries),str(process)]);assert not r.stderr
            records=[json.loads(line) for line in r.stdout.splitlines()];assert len(records)==37
            assert records[0]['phase']=='gate' and records[0]['core_memory_equal']
            paired=records[1:];verify_process(paired)
            for row in paired:row.update(corpus=corpus,process=process)
            raw.extend(paired);save(timing/'raw.json',raw)
            print('timing',corpus,'process',process+1,'done',flush=True)
        save(timing/'summary.json',summary(raw));save(timing/'identity.json',meta)
    assert len(raw)==540
    for p,h in meta['sources'].items():assert sha(WORKSPACE/p)==h,('source drift',p)
    save(timing/'summary.json',summary(raw));save(timing/'identity.json',meta)
    log.close();data=(timing/'commands.log').read_bytes()
    with (timing/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as gz:gz.write(data)
    assert gzip.decompress((timing/'commands.log.gz').read_bytes())==data
    save(timing/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(timing/'commands.log.gz')})
    (timing/'commands.log').unlink()
    print('completed',len(raw),'timing rows; no source/default mutation',flush=True)


if __name__=='__main__':main()

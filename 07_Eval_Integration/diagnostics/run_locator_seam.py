"""B-stage same-layout seam gates, then one bounded no-optimization A/B. WSL/Linux."""
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
ARMS=['old_model','new_model','new_model_AA','old_directory','new_directory','new_directory_AA']
PAIRS=[('new_model','old_model'),('new_directory','old_directory'),
       ('new_model_AA','new_model'),('new_directory_AA','new_directory')]
GATE_CELLS=['tranco_full_100000','tranco_64k_100000','mixed_full_100000','skew_full_100000']
TIMING_CELLS=['tranco_64k_100000','mixed_full_100000','skew_full_100000']


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,obj):p.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n')


def verify_process(rows):
    assert len(rows)==36 and len({r['checksum'] for r in rows})==1
    assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in rows)
    pairs=collections.Counter()
    for rep in range(6):
        seq=sorted((r for r in rows if r['repeat']==rep),key=lambda r:r['position'])
        assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==set(ARMS)
        for x,y in zip(seq,seq[1:]):pairs[x['arm'],y['arm']]+=1
    assert len(pairs)==30 and set(pairs.values())=={1}
    for arm in ARMS:assert sorted(r['position'] for r in rows if r['arm']==arm)==list(range(6))


def summarize(raw):
    result=[]
    for cell in TIMING_CELLS:
        rows=[r for r in raw if r['cell']==cell]
        if len(rows)!=180:continue
        ratios={}
        for a,b in PAIRS:
            medians=[]
            for process in range(5):
                times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                medians.append(statistics.median(times[rep,a]/times[rep,b] for rep in range(6)))
            ratios[a+'/'+b]={'process_medians':medians,'median':statistics.median(medians),
                'faster':sum(x<1 for x in medians),'slower':sum(x>1 for x in medians)}
        bands={policy:max(max(x,1/x) for x in ratios['new_'+policy+'_AA/new_'+policy]['process_medians'])
               for policy in ['model','directory']}
        for name,r in ratios.items():
            policy='directory' if 'directory' in name else 'model';band=bands[policy]
            r['clear_slowdown']=r['median']>1.05 and r['slower']>=4 and r['median']>band
            r['clear_gain']=r['median']<.95 and r['faster']>=4 and 1/r['median']>band
        result.append({'cell':cell,'ratios':ratios,'aa_observed_bands':bands,
            'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}})
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='locator_seam_20261018');a=parser.parse_args()
    assert Path(a.output).name==a.output
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    prior=json.loads((ROOT/'results/static_interface_20261012c/identity.json').read_text())
    original=UPSTREAM/'static_physical_index.hpp'
    assert sha(original)==prior['sources'][str(original.relative_to(WORKSPACE))]
    old_hash=sha(original);new_hash=sha(UPSTREAM/'physical_locator_index.hpp')
    adapter_hash=sha(ROOT/'diagnostics/locator_seam_adapter.hpp')
    meta={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'cpu':min(os.sched_getaffinity(0)),'commands':[],'sources':{},'inputs':{},'binaries':{},
        'protocol':'5 processes x 6 even Williams rows; immediate warm-up; 200k operations; separate same-instance A/A for model/directory',
        'scope':'B-stage experimental seam; unchanged layout/trainer/scan; no ART/HOT ranking; no production/default change',
        'descriptor':{'old_core':old_hash,'new_core':new_hash,'adapter':adapter_hash}}
    log=(out/'commands.log').open('x')
    def run(command,timeout=900,env=None):
        command=list(map(str,command));meta['commands'].append(command)
        log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=timeout,env=env)
        log.write(r.stdout);log.write(r.stderr);log.flush()
        if r.returncode:
            save(out/'failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr[-12000:]})
            save(out/'identity.json',meta);raise RuntimeError('command failed; see failure.json')
        return r
    meta['compiler']=run(['g++','--version']).stdout.splitlines()[0]
    assert meta['compiler']==prior['compiler']
    common=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Idiagnostics',
        '-DLOCATOR_OLD_SHA256="'+old_hash+'"','-DLOCATOR_NEW_SHA256="'+new_hash+'"',
        '-DLOCATOR_ADAPTER_SHA256="'+adapter_hash+'"']
    files={Path(__file__),ROOT/'diagnostics/verify_locator_seam.py',ROOT/'diagnostics/LOCATOR_SEAM_SPEC_20261018.md',
        WORKSPACE/'docs/PHYSICAL_LOCATOR_CONTRACT_20261018.md'}
    def compile_exe(name,sources,flags):
        objects=[]
        for i,source in enumerate(sources):
            obj=build/(name+'_'+str(i)+'.o');dep=obj.with_suffix('.d')
            run(['g++']+common+flags+['-MMD','-MF',dep,'-c',source,'-o',obj])
            files.update((ROOT/p).resolve() for p in dep.read_text().replace('\\\n',' ').split(':',1)[1].split())
            objects.append(obj)
        exe=build/name;run(['g++','-march=x86-64-v3']+flags+objects+['-o',exe]);meta['binaries'][name]=sha(exe)
        return exe
    print('compiling seam, common acceptance and sanitizer gates',flush=True)
    accept=compile_exe('acceptance',['adapters/common/contract_acceptance.cpp'],['-O2',
        '-DACCEPTANCE_FACTORY_HEADER="locator_seam_adapter.hpp"','-DACCEPTANCE_FACTORY_SYMBOL=make_locator_seam_index',
        '-DACCEPTANCE_SAMPLE_PARAMETER=lookup'])
    gate_sources=['diagnostics/check_locator_seam.cpp','learned-index/src/unified/runner.cpp']
    check=compile_exe('check',gate_sources,['-O2'])
    san=compile_exe('san_check',gate_sources,['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])
    bench=compile_exe('bench',['diagnostics/bench_locator_seam.cpp','learned-index/src/unified/runner.cpp'],['-O3'])
    meta['sources']={str(p.relative_to(WORKSPACE)):sha(p) for p in sorted(files)}
    for cell in GATE_CELLS:
        inp=prior['inputs'][cell];rows=WORKSPACE/inp['rows_path'];queries=WORKSPACE/inp['queries_path']
        assert sha(rows)==inp['rows_sha256'] and sha(queries)==inp['queries']['sha256']
        meta['inputs'][cell]={'rows_path':inp['rows_path'],'queries_path':inp['queries_path'],
            'rows_sha256':sha(rows),'queries_sha256':sha(queries),'rows':inp['rows'],'queries':100000}
    save(out/'identity.json',meta)
    r=run([accept]);assert not r.stderr;(out/'acceptance.txt').write_text(r.stdout)
    print(r.stdout.splitlines()[-1],flush=True)
    gates=[]
    for cell in GATE_CELLS:
        inp=meta['inputs'][cell];args=[WORKSPACE/inp['rows_path'],WORKSPACE/inp['queries_path']]
        r=run(['taskset','-c',meta['cpu'],check]+args);assert not r.stderr;normal=json.loads(r.stdout)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        s=run(['taskset','-c',meta['cpu'],san]+args,env=env);assert not s.stderr;sanitized=json.loads(s.stdout)
        assert normal==sanitized and normal['work_checks']==300000
        gates.append({'cell':cell,'normal':normal,'sanitized':sanitized,'sanitizer_rc':s.returncode,'sanitizer_stderr_bytes':len(s.stderr)})
        save(out/'gates.json',gates);print('gate',cell,'passed',normal['checks'],'checks',flush=True)
    # No timing starts until every requested correctness gate has passed.
    raw=[]
    for cell in TIMING_CELLS:
        inp=meta['inputs'][cell];args=[WORKSPACE/inp['rows_path'],WORKSPACE/inp['queries_path']]
        for process in range(5):
            r=run(['taskset','-c',meta['cpu'],bench]+args+[process]);assert not r.stderr
            records=[json.loads(line) for line in r.stdout.splitlines()];assert len(records)==37
            assert records[0]['phase']=='gate' and records[0]['new_bytes']==records[0]['old_bytes']+8
            assert records[0]['verified_answers']==6*(inp['rows']+100000)
            paired=records[1:];verify_process(paired)
            for row in paired:row.update(cell=cell,process=process)
            raw.extend(paired);save(out/'raw.json',raw)
            print('timing',cell,process+1,'/5 completed',flush=True)
        save(out/'summary.json',summarize(raw))
    assert len(raw)==540
    for p,h in meta['sources'].items():assert sha(WORKSPACE/p)==h,('source drift',p)
    save(out/'identity.json',meta);save(out/'summary.json',summarize(raw));log.close()
    data=(out/'commands.log').read_bytes()
    with (out/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(data)
    assert gzip.decompress((out/'commands.log.gz').read_bytes())==data
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink();print('completed gates and 540 timing rows',flush=True)


if __name__=='__main__':main()

"""D1 complete-query candidate: gates before a single registered paired matrix."""
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
ARMS=['B_learned','B_AA','B_wide','B_directory','D1','D1_AA']
PAIRS=[('D1','B_learned'),('D1','B_wide'),('D1','B_directory'),('B_AA','B_learned'),('D1_AA','D1')]
GATE_CELLS=['tranco_full_100000','tranco_64k_100000','mixed_full_100000','skew_full_100000']
CELLS=[f'{c}_{s}_{n}' for c,s in [('skew','full'),('mixed','full'),('tranco','full'),('tranco','64k')] for n in [10000,100000]]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,obj):p.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n')


def verify_process(rows):
    assert len(rows)==36 and len({r['checksum'] for r in rows})==1
    assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in rows)
    pairs=collections.Counter()
    for repeat in range(6):
        seq=sorted((r for r in rows if r['repeat']==repeat),key=lambda r:r['position'])
        assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==set(ARMS)
        for a,b in zip(seq,seq[1:]):pairs[a['arm'],b['arm']]+=1
    assert len(pairs)==30 and set(pairs.values())=={1}
    for a in ARMS:assert sorted(r['position'] for r in rows if r['arm']==a)==list(range(6))


def summary(raw):
    result=[]
    for cell in CELLS:
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
        bands={name:max(max(x,1/x) for x in ratios[name]['process_medians']) for name in ['B_AA/B_learned','D1_AA/D1']}
        f=max(bands.values())
        for r in ratios.values():
            r['clear_gain']=r['median']<=.90 and r['faster']>=4 and 1/r['median']>f
            r['clear_regression']=r['median']>1.05 and r['slower']>=4 and r['median']>f
        result.append({'cell':cell,'ratios':ratios,'aa_bands':bands,'observed_band':f,
            'target_gain':all(ratios['D1/'+a]['clear_gain'] for a in ['B_learned','B_wide']),
            'risk_regression':any(ratios['D1/'+a]['clear_regression'] for a in ['B_learned','B_wide']),
            'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}})
    return result


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='collision_refinement_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output
    entry=ROOT/'results/collision_refinement_entry_20261018';decision=json.loads((entry/'decision.json').read_text())
    assert decision['entry_passed']
    entry_meta=json.loads((entry/'identity.json').read_text())
    for path,h in entry_meta['sources'].items():assert sha(WORKSPACE/path)==h,('entry source drift',path)
    entry_data={r['cell']:r['data'] for r in json.loads((entry/'results.json').read_text())}
    prior=json.loads((ROOT/'results/static_interface_20261012c/identity.json').read_text())
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    base_hash=sha(UPSTREAM/'physical_locator_index.hpp')
    bundle_hash=hashlib.sha256((sha(ROOT/'diagnostics/collision_refined_index.hpp')+sha(ROOT/'diagnostics/collision_refinement.hpp')).encode()).hexdigest()
    adapter_hash=sha(ROOT/'diagnostics/collision_refinement_adapter.hpp')
    meta={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'cpu':min(os.sched_getaffinity(0)),'sources':{},'inputs':{},'binaries':{},'commands':[],
        'entry_evidence':{str((entry/n).relative_to(WORKSPACE)):sha(entry/n) for n in ['identity.json','results.json','decision.json','domain.json']},
        'descriptor':{'B_core':base_hash,'D1_bundle':bundle_hash,'adapter':adapter_hash},
        'scope':'D1 one local prefix level; complete v1 queries; B controls same layout; no ART/HOT ranking; production untouched',
        'protocol':'5 processes x 6 even Williams rows; 200k immediate warm-up and 200k timed finds; B and D1 same-instance A/A; max symmetric AA band'}
    files={Path(__file__),ROOT/'diagnostics/verify_collision_refinement.py',ROOT/'diagnostics/COLLISION_REFINEMENT_SPEC_20261018.md'}
    log=(out/'commands.log').open('x')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=900,env=env)
        log.write(r.stdout);log.write(r.stderr);log.flush()
        if r.returncode:
            save(out/'failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr[-12000:]});save(out/'identity.json',meta)
            raise RuntimeError('command failed; see failure.json')
        return r
    meta['compiler']=run(['g++','--version']).stdout.splitlines()[0];assert meta['compiler']==prior['compiler']
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Idiagnostics',
        '-DREFINEMENT_BASE_SHA256="'+base_hash+'"','-DREFINEMENT_CORE_SHA256="'+bundle_hash+'"',
        '-DREFINEMENT_ADAPTER_SHA256="'+adapter_hash+'"']
    def compile_exe(name,sources,opt):
        objects=[]
        for i,source in enumerate(sources):
            obj=build/(name+str(i)+'.o');dep=obj.with_suffix('.d')
            run(['g++']+flags+opt+['-MMD','-MF',dep,'-c',source,'-o',obj])
            files.update((ROOT/x).resolve() for x in dep.read_text().replace('\\\n',' ').split(':',1)[1].split());objects.append(obj)
        exe=build/name;run(['g++','-march=x86-64-v3']+opt+objects+['-o',exe]);meta['binaries'][name]=sha(exe);return exe
    print('compiling common acceptance, full gates and complete-query benchmark',flush=True)
    acceptance=compile_exe('acceptance',['adapters/common/contract_acceptance.cpp'],['-O2',
        '-DACCEPTANCE_FACTORY_HEADER="collision_refinement_adapter.hpp"','-DACCEPTANCE_FACTORY_SYMBOL=make_collision_refined_index',
        '-DACCEPTANCE_SAMPLE_PARAMETER=lookup'])
    source=['diagnostics/check_collision_refinement.cpp','learned-index/src/unified/runner.cpp']
    check=compile_exe('check',source,['-O2']);san=compile_exe('san_check',source,['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])
    bench=compile_exe('bench',['diagnostics/bench_collision_refinement.cpp','learned-index/src/unified/runner.cpp'],['-O3'])
    meta['sources']={str(x.relative_to(WORKSPACE)):sha(x) for x in sorted(files)}
    for cell in CELLS:
        item=prior['inputs'][cell];rows=WORKSPACE/item['rows_path'];queries=WORKSPACE/item['queries_path']
        assert sha(rows)==item['rows_sha256'] and sha(queries)==item['queries']['sha256']
        meta['inputs'][cell]={'rows_path':item['rows_path'],'queries_path':item['queries_path'],
            'rows_sha256':sha(rows),'queries_sha256':sha(queries),'rows':item['rows'],'queries':item['queries']['count']}
    save(out/'identity.json',meta)
    accepted=run([acceptance]);assert not accepted.stderr;(out/'acceptance.txt').write_text(accepted.stdout)
    print(accepted.stdout.splitlines()[-1],flush=True);gates=[]
    for cell in GATE_CELLS:
        item=meta['inputs'][cell];args=[WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
        normal=run(['taskset','-c',meta['cpu'],check]+args);assert not normal.stderr;data=json.loads(normal.stdout)
        sanitized=run(['taskset','-c',meta['cpu'],san]+args,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert not sanitized.stderr and json.loads(sanitized.stdout)==data
        for key in ['old_blocks','new_blocks','covered','local_narrow','parent_corrections','family_steps','local_steps','prefix_bytes','directory_steps']:
            assert data[key]==entry_data[cell][key],('entry/actual work mismatch',cell,key)
        assert data['refinement_bytes']==entry_data[cell]['extra_bytes'] and data['work_checks']==100000
        gates.append({'cell':cell,'normal':data,'sanitized':json.loads(sanitized.stdout),'sanitizer_rc':0,'sanitizer_stderr_bytes':0})
        save(out/'gates.json',gates);print('gate',cell,'passed',data['checks'],'checks',flush=True)
    raw=[]
    for cell in CELLS:
        item=meta['inputs'][cell];args=[WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
        for process in range(5):
            r=run(['taskset','-c',meta['cpu'],bench]+args+[process]);assert not r.stderr
            records=[json.loads(line) for line in r.stdout.splitlines()];assert len(records)==37
            head=records[0];assert head['phase']=='gate' and head['verified_answers']==6*(item['rows']+item['queries'])
            assert head['D1_bytes']==head['base_bytes']+entry_data[cell]['extra_bytes']
            paired=records[1:];verify_process(paired)
            for row in paired:row.update(cell=cell,process=process)
            raw.extend(paired);save(out/'raw.json',raw);print('timing',cell,process+1,'/5 done',flush=True)
        save(out/'summary.json',summary(raw))
    assert len(raw)==1440
    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source drift',path)
    cells=summary(raw);save(out/'summary.json',cells)
    target=all(c['target_gain'] for c in cells if c['cell'].startswith('skew_'))
    risks=[c['cell'] for c in cells if not c['cell'].startswith('skew_') and c['risk_regression']]
    save(out/'decision.json',{'target_gain':target,'risk_regressions':risks,'matrix_eligible':target and not risks,
        'external_validation_pending':True,'production_adopted':False,'original_S3_failure_preserved':True,'ART_HOT_retested':False})
    save(out/'identity.json',meta);log.close();data=(out/'commands.log').read_bytes()
    with (out/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(data)
    assert gzip.decompress((out/'commands.log.gz').read_bytes())==data
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink();print('completed 1440 timing rows',flush=True)


if __name__=='__main__':main()

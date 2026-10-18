"""C1 single mechanism work/space entrance. No timed index candidate."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
UPSTREAM=WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'
MAIN={'tranco_64k_10000','tranco_64k_100000','mixed_full_10000'}


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,obj):p.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n')


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='segment_navigation_entry_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    prior=json.loads((ROOT/'results/static_interface_20261012c/identity.json').read_text())
    frozen=json.loads((ROOT/'results/locator_seam_20261018/identity.json').read_text())
    for name in ['physical_locator_index.hpp','static_physical_index.hpp']:
        path=UPSTREAM/name;assert sha(path)==frozen['sources'][str(path.relative_to(WORKSPACE))]
    meta={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'cpu':min(os.sched_getaffinity(0)),'sources':{},'binaries':{},'inputs':{},'commands':[],
        'scope':'C1 full-key segment-cut navigation entrance; work/space only; no timing or adoption'}
    files={Path(__file__),ROOT/'diagnostics/SEGMENT_NAV_ENTRY_SPEC_20261018.md'}
    log=(out/'commands.log').open('x')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=900,env=env)
        log.write(r.stdout);log.write(r.stderr);log.flush()
        if r.returncode:
            save(out/'failure.json',{'rc':r.returncode,'command':command,'stderr':r.stderr[-8000:]})
            save(out/'identity.json',meta);raise RuntimeError('entry failed; see failure.json')
        return r
    meta['compiler']=run(['g++','--version']).stdout.splitlines()[0];assert meta['compiler']==prior['compiler']
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Idiagnostics']
    def compile_exe(name,opt):
        objects=[]
        for i,source in enumerate(['diagnostics/audit_segment_navigation.cpp','learned-index/src/unified/runner.cpp']):
            obj=build/(name+str(i)+'.o');dep=obj.with_suffix('.d')
            run(['g++']+flags+opt+['-MMD','-MF',dep,'-c',source,'-o',obj])
            files.update((ROOT/x).resolve() for x in dep.read_text().replace('\\\n',' ').split(':',1)[1].split());objects.append(obj)
        exe=build/name;run(['g++','-march=x86-64-v3']+opt+objects+['-o',exe]);meta['binaries'][name]=sha(exe);return exe
    exe=compile_exe('audit',['-O2']);san=compile_exe('san_audit',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])
    meta['sources']={str(x.relative_to(WORKSPACE)):sha(x) for x in sorted(files)};save(out/'identity.json',meta)
    normal=run([exe]);assert not normal.stderr
    sanitized=run([san],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    assert not sanitized.stderr and json.loads(normal.stdout)==json.loads(sanitized.stdout)
    save(out/'domain.json',{'normal':json.loads(normal.stdout),'sanitized':json.loads(sanitized.stdout),'stderr_bytes':0,'rc':0})
    print('domain passed',normal.stdout.strip(),flush=True)
    results=[]
    for corpus,scale in [('tranco','64k'),('mixed','full'),('tranco','full'),('skew','full')]:
        for count in [10000,100000]:
            cell=f'{corpus}_{scale}_{count}';item=prior['inputs'][cell]
            rows=WORKSPACE/item['rows_path'];queries=WORKSPACE/item['queries_path']
            assert sha(rows)==item['rows_sha256'] and sha(queries)==item['queries']['sha256']
            meta['inputs'][cell]={'rows_path':item['rows_path'],'queries_path':item['queries_path'],
                'rows_sha256':sha(rows),'queries_sha256':sha(queries)}
            r=run(['taskset','-c',meta['cpu'],exe,rows,queries]);assert not r.stderr;data=json.loads(r.stdout)
            old=data['old'];flat=data['flat'];n=data['queries']
            tests={'model_search_coverage':data['model_search_queries']/n>=.80,
                'navigation_comparisons':flat['steps']<=1.10*(old['route_steps']+old['model_steps']),
                'full_compare_growth':flat['full_compares']-old['route_full_compares']<=.5*n,
                'space_budget':data['navigation_bytes']<=.05*data['core_bytes']}
            results.append({'cell':cell,'main':cell in MAIN,'data':data,'entry_tests':tests,'passed':all(tests.values())})
            save(out/'results.json',results);save(out/'identity.json',meta)
            print(cell,tests,flush=True)
    for name,h in meta['sources'].items():assert sha(WORKSPACE/name)==h,('source drift',name)
    decision={'mechanism':'C1: fused full-key segment-cut navigation','entry_passed':all(r['passed'] for r in results if r['main']),
        'timing_performed':False,'production_changed':False,'original_S3_failure_preserved':True,
        'main_cells':sorted(MAIN),'failed_main_cells':[r['cell'] for r in results if r['main'] and not r['passed']]}
    save(out/'decision.json',decision);save(out/'identity.json',meta);log.close();data=(out/'commands.log').read_bytes()
    with (out/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(data)
    assert gzip.decompress((out/'commands.log.gz').read_bytes())==data
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink();print(json.dumps(decision),flush=True)


if __name__=='__main__':main()

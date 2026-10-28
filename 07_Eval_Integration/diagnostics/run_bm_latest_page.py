"""Original upstream BM integration, complete dynamic semantic gates; no timings."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
UPSTREAM=ROOT/'upstream_snapshots/model_e8e05411'
BENCHMARK=ROOT/'upstream_snapshots/benchmark_003a55bf'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='bm_latest_page_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output
    base=read(ROOT/'results/f5_coupled_page_20261018/identity.json');sources=dict(base['sources'])
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest,('frozen F5 drift',path)
    lock=read(ROOT/'results/upstream_update_audit_20261018/identity.json');pin=lock['repositories']['model']
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=UPSTREAM,text=True).strip()==pin['commit']
    upstream_identity=[]
    for name in ('src/block_model.h','src/block_model.cpp','src/key_bytes.h','src/query_cost.h'):
        path=UPSTREAM/name;data=path.read_bytes()
        assert data==subprocess.check_output(['git','show','HEAD:'+name],cwd=UPSTREAM)
        assert sha(path)==pin['tracked_files_sha256'][name]
        upstream_identity.append({'path':name,'compiled_sha256':sha(path),'matches_fixed_git_blob':True})
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    for name in ('src/index_contract.h','src/unified/runner.h','src/unified/runner.cpp'):
        path=BENCHMARK/name;assert sha(path)==lock['repositories']['benchmark']['tracked_files_sha256'][name]
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    sources[str((ROOT/'results/upstream_update_audit_20261018/identity.json').relative_to(WORKSPACE))]=sha(ROOT/'results/upstream_update_audit_20261018/identity.json')
    for path in [Path(__file__),*[ROOT/'diagnostics'/n for n in ('generate_bm_page.py','bm_page_core.hpp','bm_page_root.hpp','bm_page_index.hpp','check_bm_page.cpp','BM_PAGE_SPEC_20261018.md','BM_LATEST_SPEC_20261018.md','prepare_bm_latest.py')]]:
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    inputs=base['inputs']
    for item in inputs.values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'sources':sources,'inputs':inputs,'artifacts':{},'commands':[],'upstream_commit':pin['commit'],
          'benchmark_commit':lock['repositories']['benchmark']['commit'],'model_snapshot':str(UPSTREAM.relative_to(WORKSPACE)),
          'fit':{'block_size':1,'bytes':7,'segment_budget':8,'max_error':1},'upstream_identity':upstream_identity,
          'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
          'limits':['single-threaded exception safety','self accounting excludes node/allocator metadata and builder scratch','no timing or adoption inference']}
    for path in sources:
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    def save(name,obj):(out/name).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
    save('identity.json',meta);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,env=env,timeout=1200)
        log.write(r.stdout+r.stderr);log.flush()
        if r.returncode:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});save('identity.json',meta)
            raise RuntimeError('BM gate failed; see failure.json')
        assert not r.stderr;return [json.loads(s) for s in r.stdout.splitlines() if s.startswith('{')]
    flags=['-pthread','-std=c++17','-march=x86-64-v3','-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),
           '-I'+str(BENCHMARK/'src'),'-I'+str(UPSTREAM/'src')]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');gates={}
    for kind,opts in [('normal',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=build/('bm_check_'+kind);print('compiling',kind,flush=True)
        run(['g++']+flags+opts+[ROOT/'diagnostics/check_bm_page.cpp',UPSTREAM/'src/block_model.cpp',BENCHMARK/'src/unified/runner.cpp','-o',exe])
        meta['artifacts'][str(exe.relative_to(out))]=sha(exe);gates[kind]={}
        for name,item in [('synthetic',None)]+list(inputs.items()):
            records=run([exe]+([] if item is None else [WORKSPACE/item['rows_path']]),env)
            assert records[-1]['status']=='passed';gates[kind][name]=records;save('gates.json',gates)
            print(kind,name,records[-1],flush=True)
    assert gates['normal']==gates['sanitized']
    space=[]
    for name,records in gates['normal'].items():
        if name=='synthetic':continue
        row=next(r for r in records if r['phase']=='corpus');work=next(r for r in records if r['phase']=='bm_work')
        assert work['bm_calls']>0 and work['optimized_blocks']<=work['base_blocks']
        ratio=row['initial_slotted_bytes']/row['initial_d1_bytes']
        space.append({'cell':name,'BM_over_D1':ratio,'within_10pct':ratio<=1.10})
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    for item in inputs.values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    save('verification.json',{'status':'passed','checks_per_build':{k:v[-1]['checks'] for k,v in gates['normal'].items()},
                              'actual_BM_used':True,'upstream_unchanged':True,'production_adopted':False})
    save('entry_decision.json',{'space_cells':space,'semantic_timing_allowed':True,'production_adopted':False})
    save('identity.json',meta);log.close()

if __name__=='__main__':main()

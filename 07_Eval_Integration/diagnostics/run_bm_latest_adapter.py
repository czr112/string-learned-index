"""Shared v1 dynamic acceptance of the BM wrapper, no timing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
MODEL=ROOT/'upstream_snapshots/model_e8e05411'
BENCHMARK=ROOT/'upstream_snapshots/benchmark_003a55bf'
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='bm_latest_adapter_20261018');a=p.parse_args();assert Path(a.output).name==a.output
    entry=ROOT/'results/bm_latest_page_20261018';assert read(entry/'independent_verification.json')['original_BM_used']
    base=read(entry/'identity.json');sources=dict(base['sources'])
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    additions=[Path(__file__),ROOT/'diagnostics/prepare_bm_adapter.py',ROOT/'diagnostics/bm_page_adapter.hpp',
               ROOT/'adapters/common/contract_acceptance.cpp',BENCHMARK/'src/index_contract.h']
    for path in additions:sources[str(path.relative_to(WORKSPACE))]=sha(path)
    core_paths=[ROOT/'diagnostics'/n for n in ('bm_page_index.hpp','bm_page_root.hpp','bm_page_core.hpp')]
    core_paths += [MODEL/'src'/n for n in ('block_model.h','block_model.cpp','key_bytes.h','query_cost.h')]
    core_paths += [WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp']
    bundle=hashlib.sha256(''.join(sha(p) for p in core_paths).encode()).hexdigest();adapter=ROOT/'diagnostics/bm_page_adapter.hpp'
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'sources':sources,'artifacts':{},'commands':[],'upstream_commit':base['upstream_commit'],'benchmark_commit':base['benchmark_commit'],
          'core_bundle_paths':[str(p.relative_to(WORKSPACE)) for p in core_paths],'core_bundle_sha256':bundle,'adapter_sha256':sha(adapter)}
    for path in sources:
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    def save(name,obj):(out/name).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
    save('identity.json',meta);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,env=env,timeout=900);log.write(r.stdout+r.stderr);log.flush()
        if r.returncode:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});save('identity.json',meta)
            raise RuntimeError('BM common acceptance failed')
        assert not r.stderr;return r.stdout
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(ROOT/'diagnostics'),'-I'+str(BENCHMARK/'src'),'-I'+str(MODEL/'src'),
           '-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),
           '-DBM_CORE_SHA256="'+bundle+'"','-DBM_ADAPTER_SHA256="'+sha(adapter)+'"',
           '-DACCEPTANCE_FACTORY_HEADER="bm_page_adapter.hpp"','-DACCEPTANCE_FACTORY_SYMBOL=make_bm_page_index']
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');results={}
    for kind,opts in [('normal',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=build/('bm_acceptance_'+kind);print('compiling',kind,flush=True)
        run(['g++']+flags+opts+[ROOT/'adapters/common/contract_acceptance.cpp',MODEL/'src/block_model.cpp','-o',exe])
        meta['artifacts'][str(exe.relative_to(out))]=sha(exe)
        accepted=run([exe],env);assert '99/99 checks passed' in accepted
        results[kind]=accepted;(out/('acceptance_'+kind+'.txt')).write_text(accepted,encoding='utf-8');print(kind,'99/99 passed',flush=True)
    assert results['normal']==results['sanitized']
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    save('verification.json',{'status':'passed','shared_acceptance_per_build':99,'original_BM_used':True,'production_registered':False,'production_adopted':False})
    save('identity.json',meta);log.close()

if __name__=='__main__':main()

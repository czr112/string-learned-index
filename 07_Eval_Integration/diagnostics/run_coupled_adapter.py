"""F6A experimental v1 bridge acceptance, without timing or production registration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='f6a_coupled_adapter_20261018');args=p.parse_args()
    assert Path(args.output).name==args.output
    entry=ROOT/'results/f5_coupled_page_read_20261018'
    assert read(entry/'decision.json')['dynamic_gate_allowed']
    assert read(entry/'independent_verification.json')['status']=='verified'
    base=read(entry/'identity.json');sources=dict(base['sources'])
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest,('frozen F5 drift',path)
    additions=[Path(__file__),ROOT/'adapters/common/contract_acceptance.cpp',ROOT/'learned-index/src/index_contract.h']
    additions += [ROOT/'diagnostics'/n for n in ('coupled_page_adapter.hpp','check_coupled_adapter.cpp','F6A_COUPLED_ADAPTER_SPEC_20261018.md')]
    for path in additions:sources[str(path.relative_to(WORKSPACE))]=sha(path)
    core_paths=[ROOT/'diagnostics'/n for n in ('coupled_page_index.hpp','coupled_root.hpp','route_attribution_page.hpp','collision_refinement.hpp')]
    core_paths += [WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'/n for n in ('physical_locator_index.hpp','static_physical_index.hpp')]
    bundle=hashlib.sha256(''.join(sha(p) for p in core_paths).encode()).hexdigest()
    for item in base['inputs'].values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'sources':sources,'inputs':base['inputs'],'artifacts':{},'commands':[],
          'core_bundle_sha256':bundle,'core_bundle_paths':[str(p.relative_to(WORKSPACE)) for p in core_paths],
          'f5_identity_sha256':sha(entry/'identity.json'),'f5_decision_sha256':sha(entry/'decision.json'),
          'adapter_sha256':sha(ROOT/'diagnostics/coupled_page_adapter.hpp'),
          'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0]}
    for path in sources:
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    def save(name,obj):(out/name).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
    save('identity.json',meta);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,env=env,timeout=900)
        log.write(r.stdout+r.stderr);log.flush()
        if r.returncode:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});save('identity.json',meta)
            raise RuntimeError('F6A failed; see failure.json')
        assert not r.stderr;return r.stdout
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(ROOT/'diagnostics'),
           '-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),'-I'+str(ROOT/'learned-index/src'),
           '-DCOUPLED_CORE_SHA256="'+bundle+'"','-DCOUPLED_ADAPTER_SHA256="'+meta['adapter_sha256']+'"']
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    gates={};acceptances={}
    for kind,opts in [('normal',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        acceptance=build/('acceptance_'+kind);driver=build/('corpus_'+kind);print('compiling',kind,flush=True)
        run(['g++']+flags+opts+['-DACCEPTANCE_FACTORY_HEADER="coupled_page_adapter.hpp"','-DACCEPTANCE_FACTORY_SYMBOL=make_coupled_page_index',ROOT/'adapters/common/contract_acceptance.cpp','-o',acceptance])
        run(['g++']+flags+opts+[ROOT/'diagnostics/check_coupled_adapter.cpp',ROOT/'learned-index/src/unified/runner.cpp','-o',driver])
        for exe in (acceptance,driver):meta['artifacts'][str(exe.relative_to(out))]=sha(exe)
        accepted=run([acceptance],env);assert '99/99 checks passed' in accepted
        (out/('acceptance_'+kind+'.txt')).write_text(accepted,encoding='utf-8');acceptances[kind]=accepted
        gates[kind]={};print(kind,'shared acceptance 99/99',flush=True)
        for name,item in base['inputs'].items():
            result=json.loads(run([driver,WORKSPACE/item['rows_path']],env));assert result['status']=='passed'
            gates[kind][name]=result;save('gates.json',gates);print(kind,name,result,flush=True)
    assert acceptances['normal']==acceptances['sanitized'] and gates['normal']==gates['sanitized']
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    for item in base['inputs'].values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    save('verification.json',{'status':'passed','shared_acceptance_per_build':99,'corpus_checks_per_build':sum(r['checks'] for r in gates['normal'].values()),
                              'production_registered':False,'production_adopted':False,'dynamic_timing_started':False})
    save('identity.json',meta);log.close()

if __name__=='__main__':main()

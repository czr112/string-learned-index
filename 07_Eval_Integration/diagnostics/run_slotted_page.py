"""F3 semantics, coverage, locality, and space entry. No performance measurement."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
UPSTREAM=WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='f3_slotted_page_20261018');args=parser.parse_args()
    assert Path(args.output).name==args.output
    frozen=read(ROOT/'results/f2_local_page_read_20261018/identity.json')
    for path,digest in frozen['sources'].items():assert sha(WORKSPACE/path)==digest,('frozen drift',path)
    e2=read(ROOT/'results/e2_generalization_20261018/identity.json')
    inputs={cell:e2['inputs'][cell] for cell in ('tranco64_norepl','mixed_full_norepl','skew_full_norepl')}
    for item in inputs.values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    sources=[Path(__file__),*[ROOT/'diagnostics'/name for name in
        ('generate_slotted_page_core.py','slotted_page_core.hpp','slotted_page_index.hpp','check_slotted_page.cpp',
         'F3_SLOTTED_PAGE_SPEC_20261018.md','collision_refined_index.hpp','collision_refinement.hpp','static_input.hpp')],
        UPSTREAM/'static_physical_index.hpp',UPSTREAM/'physical_locator_index.hpp',
        *[ROOT/'learned-index/src'/name for name in ('unified/runner.cpp','unified/runner.h','index_contract.h')]]
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    identity={'sources':{str(path.relative_to(WORKSPACE)):sha(path) for path in sources},'inputs':inputs,
              'commands':[],'artifacts':{},'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
              'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=WORKSPACE,text=True).strip(),
              'limits':['self-accounting excludes tree links, allocator metadata and internal builder scratch','entry only; no adoption']}
    for path in sources:
        target=out/'frozen_sources'/path.relative_to(WORKSPACE);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target)
    def save(name,obj):(out/name).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
    save('identity.json',identity);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));identity['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        result=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,timeout=600,env=env)
        log.write(result.stdout+result.stderr);log.flush()
        if result.returncode:
            save('failure.json',{'command':command,'rc':result.returncode,'stderr':result.stderr});save('identity.json',identity)
            raise RuntimeError('F3 gate failed; see failure.json')
        assert not result.stderr,result.stderr
        return result.stdout
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-I'+str(ROOT/'learned-index/src')]
    gates={}
    for name,opts in [('normal',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=build/('slotted_page_'+name);print('compiling',name,flush=True)
        run(['g++']+flags+opts+[ROOT/'diagnostics/check_slotted_page.cpp',ROOT/'learned-index/src/unified/runner.cpp','-o',exe])
        identity['artifacts'][str(exe.relative_to(out))]=sha(exe)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        records={}
        for cell,item in [('synthetic',None)]+list(inputs.items()):
            command=[exe]+([] if item is None else [WORKSPACE/item['rows_path']])
            rows=[json.loads(line) for line in run(command,env).splitlines()]
            assert rows[-1]['status']=='passed';records[cell]=rows
            print(name,cell,rows[-1],flush=True)
        gates[name]=records;save('gates.json',gates)
    assert gates['normal']==gates['sanitized']
    for path,digest in identity['sources'].items():assert sha(WORKSPACE/path)==digest
    for item in inputs.values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    cells=[]
    for cell in inputs:
        record=gates['normal'][cell][0]
        ratio=record['initial_slotted_bytes']/record['initial_d1_bytes']
        cells.append({'cell':cell,'slotted_over_D1_bytes':ratio,'slotted_over_control_bytes':record['initial_slotted_bytes']/record['initial_control_bytes'],
                      'space_gate_passed':ratio<=1.10})
    save('entry_decision.json',{'semantics_passed':True,'space_cells':cells,'timing_allowed':all(c['space_gate_passed'] for c in cells),
                               'production_adopted':False})
    save('verification.json',{'status':'passed','checks_per_build':{k:v[-1]['checks'] for k,v in gates['normal'].items()},
                              'recorded_operations_per_build':{k:v[-1]['operations'] for k,v in gates['normal'].items()}})
    save('identity.json',identity);log.close();print(json.dumps(cells),flush=True)


if __name__=='__main__':main()

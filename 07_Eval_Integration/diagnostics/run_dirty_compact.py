"""F4M semantic/local-maintenance regression gate; no timing claims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'


def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='f4m_dirty_compact_20261018');args=parser.parse_args()
    assert Path(args.output).name==args.output
    f3=read(ROOT/'results/f3_slotted_page_20261018_attempt1/identity.json')
    for path,digest in f3['sources'].items():assert sha(WORKSPACE/path)==digest,('frozen F3 drift',path)
    sources=dict(f3['sources'])
    for path in [Path(__file__),*[ROOT/'diagnostics'/name for name in ('generate_dirty_compact.py','dirty_compact_index.hpp','check_dirty_compact.cpp','F4M_DIRTY_COMPACT_SPEC_20261018.md')]]:
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    inputs=f3['inputs']
    for item in inputs.values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'sources':sources,'inputs':inputs,'commands':[],'artifacts':{},
          'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
          'limits':['no performance adoption','self-accounting excludes map/set node metadata, allocator metadata and internal builder scratch']}
    for path in sources:
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    def save(name,value):(out/name).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
    save('identity.json',meta);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,timeout=900,env=env)
        log.write(r.stdout+r.stderr);log.flush()
        if r.returncode:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});save('identity.json',meta)
            raise RuntimeError('F4M failed; see failure.json')
        assert not r.stderr;return [json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')]
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),'-I'+str(ROOT/'learned-index/src')]
    gates={};env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    for kind,opts in [('normal',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=build/('dirty_compact_'+kind);print('compiling',kind,flush=True)
        run(['g++']+flags+opts+[ROOT/'diagnostics/check_dirty_compact.cpp',ROOT/'learned-index/src/unified/runner.cpp','-o',exe])
        meta['artifacts'][str(exe.relative_to(out))]=sha(exe);gates[kind]={}
        for name,item in [('synthetic',None)]+list(inputs.items()):
            result=run([exe]+([] if item is None else [WORKSPACE/item['rows_path']]),env)
            assert result[-1]['status']=='passed';gates[kind][name]=result;save('gates.json',gates)
            print(kind,name,result[-1],flush=True)
    assert gates['normal']==gates['sanitized']
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    for item in inputs.values():assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    save('verification.json',{'status':'passed','checks_per_build':{k:v[-1]['checks'] for k,v in gates['normal'].items()},
                              'production_adopted':False,'read_budget_remeasured':False})
    save('identity.json',meta);log.close()


if __name__=='__main__':main()

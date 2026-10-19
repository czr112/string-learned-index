"""Validate measurement hooks before any index space comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WS=ROOT.parent
DIAG=ROOT/'diagnostics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='allocation_trace_fixture_20261019');a=parser.parse_args()
    assert Path(a.output).name==a.output
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    names=['allocation_trace.hpp','allocation_trace.cpp','allocation_shared_fixture.c','check_allocation_trace.cpp','run_allocation_fixture.py','BM_SPACE_SPEC_20261019.md']
    meta={'sources':{},'artifacts':{},'commands':[],'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
          'libc':subprocess.check_output(['getconf','GNU_LIBC_VERSION'],text=True).strip()}
    for name in names:
        p=DIAG/name;rel=str(p.relative_to(WS));meta['sources'][rel]=sha(p)
        dest=out/'frozen_sources'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(p.read_bytes())
    log=(out/'commands.log').open('x')
    def run(cmd):
        cmd=list(map(str,cmd));meta['commands'].append(cmd);log.write('$ '+' '.join(cmd)+'\n');log.flush()
        r=subprocess.run(cmd,cwd=WS,capture_output=True,text=True,timeout=120)
        log.write(r.stdout);log.write(r.stderr);log.flush();save(out/'identity.json',meta)
        if r.returncode:
            save(out/'failure.json',{'command':cmd,'rc':r.returncode,'stderr':r.stderr});raise RuntimeError('fixture failed; retained')
        return r.stdout
    print('compile shared C fixture and C/C++ allocation hooks',flush=True)
    run(['gcc','-O2','-fno-builtin','-shared','-fPIC',DIAG/'allocation_shared_fixture.c','-o',build/'liballocation_fixture.so'])
    run(['g++','-pthread','-std=c++17','-O2','-fno-builtin','-DBM_ALLOCATION_TRACE=1','-rdynamic',
         DIAG/'allocation_trace.cpp',DIAG/'check_allocation_trace.cpp','-L'+str(build),'-lallocation_fixture',
         '-Wl,-rpath,$ORIGIN','-o',build/'check_trace'])
    result=json.loads(run([build/'check_trace']));assert result['status']=='passed'
    assert result['checks']>=110 and result['released_all']
    for path,digest in meta['sources'].items():assert sha(WS/path)==digest
    for p in build.iterdir():meta['artifacts'][str(p.relative_to(out))]=sha(p)
    save(out/'identity.json',meta);save(out/'verification.json',result);log.close()
    print(json.dumps(result),flush=True)

if __name__=='__main__':main()

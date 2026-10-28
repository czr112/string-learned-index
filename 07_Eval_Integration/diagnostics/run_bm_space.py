"""Frozen three-arm requested/usable allocation ledger; clean build times in separate processes."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from generate_borrowed_contract_acceptance import rendered

ROOT=Path(__file__).resolve().parents[1]
WS=ROOT.parent
DIAG=ROOT/'diagnostics'
MODEL=ROOT/'upstream_snapshots/model_3894c767'
BENCH=ROOT/'upstream_snapshots/benchmark_003a55bf'
ART=WS/'02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src'
PRIOR=ROOT/'results/upstream_update_audit_20261019c'
FIXTURE=ROOT/'results/allocation_trace_fixture_20261019'
CELLS=['english_words','memetracker_urls','memetracker_phrases','tranco64_norepl']
ARMS=['bm_base','native','ART']

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def semantic(r):
    return {k:r[k] for k in ['arm','rows','queries','hits','semantic_checks','checksum','input_heap_bytes','output_heap_bytes','retains_input','self_report']}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='bm_space_20261019');args=parser.parse_args()
    assert Path(args.output).name==args.output
    assert (DIAG/'borrowed_contract_acceptance.cpp').read_text(encoding='utf-8')==rendered()
    fixture=read(FIXTURE/'identity.json');assert read(FIXTURE/'verification.json')['checks']==124
    for p,h in fixture['sources'].items():assert sha(WS/p)==h,('fixture source drift',p)
    for p,h in fixture['artifacts'].items():assert sha(FIXTURE/p)==h
    prior=read(PRIOR/'identity.json');assert prior['model_after']=='3894c767338a865d2920ba5e21283827fea1d803'
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=MODEL,text=True).strip()==prior['model_after']
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=BENCH,text=True).strip()==prior['benchmark']
    libraries=[PRIOR/'build/src/libblocklearn_adapter.a',PRIOR/'build/src/libblocklearn_core.a']
    for p in libraries:assert sha(p)==prior['artifacts'][str(p.relative_to(PRIOR))]
    inputs=prior['inputs']
    for x in inputs.values():
        assert sha(WS/x['rows_path'])==x['rows_sha256'] and sha(WS/x['queries_path'])==x['queries_sha256']
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'model':prior['model_after'],'benchmark':prior['benchmark'],'inputs':inputs,'sources':{},'artifacts':{},
          'libraries':{str(p.relative_to(WS)):sha(p) for p in libraries},'cpu':min(os.sched_getaffinity(0)),
          'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
          'libc':subprocess.check_output(['getconf','GNU_LIBC_VERSION'],text=True).strip(),
          'fixture_identity_sha256':sha(FIXTURE/'identity.json'),'fixture_verification_sha256':sha(FIXTURE/'verification.json'),
          'commands':[],'limits':['Linux glibc C/C++ replaceable allocation entry points; hidden libc and direct mmap are outside coverage',
              'requested and usable bytes are not whole-process physical RAM','RSS/HWM include common input, oracle, queries and static tracker',
              'one scale, 65536 unique keys, four fixed cells','no query or scan throughput measurement or adoption']}
    log=(out/'commands.log').open('x',encoding='utf-8')
    def run(cmd,env=None):
        cmd=list(map(str,cmd));meta['commands'].append(cmd);save(out/'identity.json',meta)
        log.write('$ '+' '.join(cmd)+'\n');log.flush()
        r=subprocess.run(cmd,cwd=WS,capture_output=True,env=env,timeout=1200)
        stdout=r.stdout.decode('utf-8',errors='backslashreplace');stderr=r.stderr.decode('utf-8',errors='backslashreplace')
        log.write(stdout);log.write(stderr);log.flush()
        if r.returncode:
            (out/'failure.stdout').write_bytes(r.stdout);(out/'failure.stderr').write_bytes(r.stderr)
            save(out/'failure.json',{'command':cmd,'returncode':r.returncode,'stderr':stderr[-12000:]})
            raise RuntimeError('stage failed; original output retained')
        return stdout
    flags=['-pthread','-std=c++17','-DNDEBUG','-march=x86-64-v3']
    flags+=['-I'+str(p) for p in [BENCH/'src',MODEL/'src',DIAG,ART,WS/'02_Code_and_Experiments/validation_e25_20260918']]
    core=[MODEL/'src'/p for p in ['block_model.cpp','residual_tags.cpp','query_cost.cpp','region_planner.cpp','learned_index.cpp','benchmark/model_factories.cpp']]
    target=[DIAG/'ledger_bm_space.cpp',ROOT/'adapters/art/art_adapter.cpp',BENCH/'src/unified/runner.cpp']
    deps=run(['g++']+flags+['-DBM_ALLOCATION_TRACE=1','-MM']+core+target+[DIAG/'allocation_trace.cpp'])
    cdeps=run(['gcc','-I'+str(ART),'-MM',ART/'art.c'])
    sources={Path(__file__),DIAG/'verify_bm_space.py',DIAG/'BM_SPACE_SPEC_20261019.md',DIAG/'generate_borrowed_contract_acceptance.py',
             ROOT/'adapters/common/contract_acceptance.cpp'}
    sources.update(WS/p for p in fixture['sources'])
    for text in [deps,cdeps]:
        for line in text.replace('\\\n',' ').splitlines():
            if ':' in line:sources.update(Path(s).resolve() for s in line.split(':',1)[1].split())
    # Audit direct calls in every frozen target dependency. C aligned APIs are intentionally not hooked.
    bypass=[]
    for p in sorted(sources):
        if p.suffix in ('.c','.cpp','.h','.hpp') and p.name not in ('allocation_trace.cpp','check_allocation_trace.cpp'):
            source=re.sub(r'/\*.*?\*/|//[^\n]*','',p.read_text(encoding='utf-8'),flags=re.S)
            bypass += [(str(p.relative_to(WS)),m.group(1)) for m in re.finditer(r'\b(mmap|mmap64|sbrk|aligned_alloc|posix_memalign|memalign|valloc|pvalloc)\s*\(',source)]
        rel=str(p.relative_to(WS));meta['sources'][rel]=sha(p)
        dest=out/'frozen_sources'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(p.read_bytes())
    meta['direct_bypass_calls']=bypass;save(out/'identity.json',meta);assert not bypass,bypass
    assert json.loads(run([FIXTURE/'build/check_trace']))['released_all']
    for flavor in ['clean','trace','san']:
        print('building',flavor,flush=True)
        opts=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if flavor=='san' else ['-O3']
        obj=build/('art_'+flavor+'.o')
        run(['gcc','-std=c11','-DNDEBUG','-march=x86-64-v3']+opts+['-I'+str(ART),'-c',ART/'art.c','-o',obj])
        extras=core if flavor=='san' else libraries
        tracking=['-DBM_ALLOCATION_TRACE=1','-rdynamic',DIAG/'allocation_trace.cpp'] if flavor=='trace' else ['-DBM_ALLOCATION_TRACE=0']
        run(['g++']+flags+opts+tracking+target+extras+[obj,'-o',build/flavor])
    for p in build.iterdir():meta['artifacts'][str(p.relative_to(out))]=sha(p)
    save(out/'identity.json',meta)
    gates=[];baseline={};contracts=[]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    def execute(flavor,arm,cell):
        x=inputs[cell];output=run(['taskset','-c',meta['cpu'],build/flavor,arm,WS/x['rows_path'],WS/x['queries_path']],env)
        r=json.loads(output.splitlines()[-1]);assert r['rows']==x['rows'] and r['queries']==x['queries']
        assert r['semantic_checks']==3*x['rows']+x['queries']
        assert r['tracking_enabled']==(flavor=='trace')
        r['cell']=cell;return r
    for flavor in ['clean','san']:
        for arm in ARMS:
            c=json.loads(run(['taskset','-c',meta['cpu'],build/flavor,arm,'--contract'],env).splitlines()[-1])
            assert c['rc']==0 and c['acceptance_checks']>=90;c['flavor']=flavor;contracts.append(c);save(out/'contracts.json',contracts)
            for cell in CELLS:
                r=execute(flavor,arm,cell);s=semantic(r)
                if flavor=='clean':baseline[arm,cell]=s
                else:assert s==baseline[arm,cell],('sanitized drift',arm,cell)
                gates.append(dict(r,flavor=flavor));save(out/'gates.json',gates)
                print('gate',flavor,arm,cell,'passed',flush=True)
    capacities=[]
    for cell in CELLS:
        for process in range(3):
            for arm in ARMS[process:]+ARMS[:process]:
                r=execute('trace',arm,cell);assert semantic(r)==baseline[arm,cell],('trace drift',arm,cell)
                capacities.append(dict(r,process=process));save(out/'capacity.json',capacities)
            print('capacity',cell,process+1,'/3',flush=True)
    timings=[]
    for cell in CELLS:
        for process in range(5):
            shift=process%3
            for position,arm in enumerate(ARMS[shift:]+ARMS[:shift]):
                r=execute('clean',arm,cell);assert semantic(r)==baseline[arm,cell]
                timings.append(dict(r,process=process,position=position));save(out/'build_times.json',timings)
            print('clean build',cell,process+1,'/5',flush=True)
    for p,h in meta['sources'].items():assert sha(WS/p)==h,('source drift',p)
    for p,h in meta['libraries'].items():assert sha(WS/p)==h
    for x in inputs.values():assert sha(WS/x['rows_path'])==x['rows_sha256'] and sha(WS/x['queries_path'])==x['queries_sha256']
    save(out/'identity.json',meta);log.close();data=(out/'commands.log').read_bytes()
    (out/'commands.log.gz').write_bytes(gzip.compress(data,mtime=0))
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink()
    print('complete: 36 capacity records, 60 clean build records; independent verification pending',flush=True)

if __name__=='__main__':main()

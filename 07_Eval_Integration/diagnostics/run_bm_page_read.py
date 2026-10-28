"""BM optimization and fresh ART/HOT direct comparison, fixed 4320-row protocol."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
MODEL=WORKSPACE/'02_Code_and_Experiments/Learned-Index-Model'
STRATEGIES=['BM_base','BM','directory','F5','ART','HOT']
ARMS=[s+suffix for s in STRATEGIES for suffix in ('','_AA')]
PAIRS=['BM/BM_base','BM/directory','BM/F5','BM/ART','BM/HOT']+[s+'_AA/'+s for s in STRATEGIES]
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def summarize(raw):
    result=[]
    for cell in sorted({r['cell'] for r in raw}):
        rows=[r for r in raw if r['cell']==cell];assert len(rows)==720
        ratios={p:[] for p in PAIRS}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==144
            table={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(table)==144
            for repeat in range(12):
                sequence=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in sequence]==list(range(12))
                assert [r['arm'] for r in sequence]==[ARMS[(x+repeat+process)%12] for x in (0,1,11,2,10,3,9,4,8,5,7,6)]
                assert len({r['checksum'] for r in sequence})==1
            for name in PAIRS:
                a,b=name.split('/');ratios[name].append(statistics.median(table[r,a]/table[r,b] for r in range(12)))
        bands={s:max(max(x,1/x) for x in ratios[s+'_AA/'+s]) for s in STRATEGIES};paired={}
        for name,points in ratios.items():
            a,b=name.split('/');band=max(bands[a.removesuffix('_AA')],bands[b]);median=statistics.median(points)
            paired[name]={'median':median,'process_medians':points,'faster':sum(x<1 for x in points),'slower':sum(x>1 for x in points),'band':band,
                          'clear_faster_5pct':median<.95 and sum(x<1 for x in points)>=4 and 1/median>band,
                          'clear_slower_5pct':median>1.05 and sum(x>1 for x in points)>=4 and median>band}
        p=paired['BM/BM_base']
        result.append({'cell':cell,'ratios':paired,'aa_bands':bands,
                       'optimization_gain_passed':p['median']<=.90 and p['faster']>=4 and 1/p['median']>p['band'],
                       'optimization_risk_failed':p['clear_slower_5pct'],
                       'BM_ART_numerical_target_met':paired['BM/ART']['median']<=1.10,
                       'ns_per_op':{s:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==s) for s in ARMS}})
    return result
def main():
    p=argparse.ArgumentParser();p.add_argument('--entry',default='bm_page_20261018');p.add_argument('--output',default='bm_page_read_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output and Path(a.entry).name==a.entry
    entry=ROOT/'results'/a.entry;assert read(entry/'verification.json')['status']=='passed'
    assert read(entry/'entry_decision.json')['semantic_timing_allowed']
    source_meta=read(entry/'identity.json');sources=dict(source_meta['sources'])
    e2=read(ROOT/'results/e2_generalization_20261018/identity.json');sources.update(e2['sources'])
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest,('frozen dependency drift',path)
    for path in (Path(__file__),ROOT/'diagnostics/bench_bm_page_read.cpp',ROOT/'diagnostics/BM_READ_SPEC_20261018.md'):
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    inputs=source_meta['inputs']
    for item in inputs.values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'sources':sources,'inputs':inputs,'artifacts':{},'commands':[],'cpu':min(os.sched_getaffinity(0)),
          'entry_path':str(entry.relative_to(WORKSPACE)),'entry_identity_sha256':sha(entry/'identity.json'),
          'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
          'upstream_commit':source_meta['upstream_commit'],'strategies':STRATEGIES,'arms':ARMS,'processes':5,'repeats':12,
          'limits':['read-only workload after existing value updates','not a mixed read/write throughput experiment','self bytes are not complete allocation bytes']}
    def save(name,obj):(out/name).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
    def archive(path):
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    for path in sources:archive(path)
    save('identity.json',meta);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None,allow_stderr=False):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,timeout=1800,env=env)
        log.write(r.stdout+r.stderr);log.flush()
        if r.returncode:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});save('identity.json',meta)
            raise RuntimeError('BM read failed; see failure.json')
        if not allow_stderr:assert not r.stderr
        return r.stdout
    def artifact(path):meta['artifacts'][str(path.relative_to(out))]=sha(path)
    def dependencies():
        for dep in build.rglob('*.o.d'):
            for value in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
                path=Path(value);path=path if path.is_absolute() else WORKSPACE/path
                if path.is_file() and path.is_relative_to(WORKSPACE) and not path.is_relative_to(build):
                    key=str(path.relative_to(WORKSPACE))
                    if key in sources:assert sources[key]==sha(path)
                    else:sources[key]=sha(path);archive(key)
    # Fresh native and sanitizer libraries: no old object reuse, all baseline code instrumented in gates.
    normal_libs=[];san_libs=[];environment=dict(os.environ)
    environment['PATH']='/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:'+environment.get('PATH','')
    os.environ['PATH']=environment['PATH'];san_env=dict(environment,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    cmake=shutil.which('cmake');assert cmake
    for kind in ('normal','sanitized'):
        for folder,opt in (('art','ART'),('hot','HOT')):
            dest=build/(folder+'_'+kind);print('fresh baseline',folder,kind,flush=True)
            options=['-DCMAKE_BUILD_TYPE='+('Release' if kind=='normal' else 'Debug'),'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-D'+opt+'_BUILD_ACCEPTANCE=ON']
            if kind=='sanitized':
                sanitizer='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
                options+=['-DCMAKE_CXX_FLAGS_DEBUG='+sanitizer,'-DCMAKE_C_FLAGS_DEBUG='+sanitizer,'-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
            run([cmake,'-S',ROOT/'adapters'/folder,'-B',dest]+options,environment,True)
            run([cmake,'--build',dest,'--parallel','2'],environment,True)
            lib=dest/('lib'+folder+'_adapter.a');exe=dest/(folder+'_adapter_acceptance')
            artifact(lib);artifact(exe);artifact(dest/'compile_commands.json')
            accepted=run([exe],san_env if kind=='sanitized' else environment);assert '99/99 checks passed' in accepted
            (out/(folder+'_'+kind+'_acceptance.txt')).write_text(accepted,encoding='utf-8')
            (normal_libs if kind=='normal' else san_libs).append(lib)
            dependencies();save('identity.json',meta)
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),
           '-I'+str(ROOT/'learned-index/src'),'-I'+str(MODEL/'src')]
    drivers={}
    for kind,opts,libs in [('normal',['-O3'],normal_libs),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],san_libs)]:
        exe=build/('bm_read_'+kind);print('compiling driver',kind,flush=True)
        run(['g++']+flags+opts+[ROOT/'diagnostics/bench_bm_page_read.cpp',MODEL/'src/block_model.cpp',ROOT/'learned-index/src/unified/runner.cpp']+libs+['-o',exe],environment,True)
        artifact(exe);drivers[kind]=exe
    def command(kind,item,state,process,mode):return ['taskset','-c',meta['cpu'],drivers[kind],WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path'],state,process,mode]
    gates=[];raw=[]
    for name,item in inputs.items():
        for state in ('clean','dirty'):
            records={}
            for kind in ('normal','sanitized'):
                records[kind]=json.loads(run(command(kind,item,state,0,'gate'),san_env if kind=='sanitized' else environment))
            for field in ('checks','rows','queries','writes','pages','BM_bytes','F5_bytes','root_publications'):
                assert records['normal'][field]==records['sanitized'][field],field
            for record in records.values():
                assert record['root_publications']==1 and record['BM_calls']+record['prefix_rejections']==record['queries'] and record['BM_calls']>0
            gates.append({'cell':name+'/'+state,**records});save('gates.json',gates)
            print('full sanitizer gate',name,state,records['normal']['checks'],flush=True)
    save('identity.json',meta)
    for name,item in inputs.items():
        for state in ('clean','dirty'):
            cell=name+'/'+state;expected=next(g['normal'] for g in gates if g['cell']==cell)
            for process in range(5):
                records=[json.loads(s) for s in run(command('normal',item,state,process,'timing'),environment).splitlines() if s.startswith('{')]
                assert len(records)==145 and records[0]==expected
                for row in records[1:]:
                    assert row['operations']==item['queries']['count'] and row['elapsed_ns']>0
                    raw.append({'cell':cell,'process':process,**row})
                save('raw.json',raw)
            print('timing',cell,'x5 complete',flush=True)
    assert len(raw)==4320;summary=summarize(raw);save('summary.json',summary)
    target=[r for r in summary if not r['cell'].startswith('tranco')];risk=[r for r in summary if r['cell'].startswith('tranco')]
    accepted=all(r['optimization_gain_passed'] for r in target) and not any(r['optimization_risk_failed'] for r in risk)
    save('decision.json',{'BM_optimization_gate_passed':accepted,'target_failures':[r['cell'] for r in target if not r['optimization_gain_passed']],
                          'risk_failures':[r['cell'] for r in risk if r['optimization_risk_failed']],
                          'BM_ART_numerical_target_met_all':all(r['BM_ART_numerical_target_met'] for r in summary),
                          'production_adopted':False,'dynamic_throughput_measured':False})
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest,('source drift',path)
    for item in inputs.values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256'] and sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    save('identity.json',meta);log.close()
    for row in summary:print(row['cell'],{k:round(row['ratios'][k]['median'],3) for k in ('BM/BM_base','BM/F5','BM/ART','BM/HOT')},flush=True)

if __name__=='__main__':main()

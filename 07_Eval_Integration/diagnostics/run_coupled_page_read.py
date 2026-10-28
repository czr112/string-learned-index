"""F3 six-arm read entry, conditional on semantic and space gates."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import shutil
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
ARMS=['live','live_AA','D1','D1_AA']


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def summarize(raw):
    result=[]
    for cell in sorted({r['cell'] for r in raw}):
        rows=[r for r in raw if r['cell']==cell];assert len(rows)==160
        values={name:[] for name in ('live/D1','live_AA/live','D1_AA/D1')}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==32
            table={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(table)==32
            for repeat in range(8):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['arm'] for r in seq]==[ARMS[(x+repeat+process)%4] for x in (0,1,3,2)]
                assert [r['position'] for r in seq]==list(range(4)) and len({r['checksum'] for r in seq})==1
            for name in values:
                a,b=name.split('/');values[name].append(statistics.median(table[r,a]/table[r,b] for r in range(8)))
        bands={a:max(max(x,1/x) for x in values[a+'_AA/'+a]) for a in ('live','D1')}
        ratios={}
        for name,points in values.items():
            a,b=name.split('/');band=max(bands[a.replace('_AA','')],bands[b]);median=statistics.median(points)
            ratios[name]={'median':median,'process_medians':points,'slower':sum(x>1 for x in points),'band':band}
        r=ratios['live/D1'];failed=r['median']>1.15 and r['slower']>=4 and r['median']>r['band']
        result.append({'cell':cell,'ratios':ratios,'aa_bands':bands,'read_budget_failed':failed,
                       'read_budget_status':'failed' if failed else 'within budget' if r['median']<=1.15 else 'unresolved',
                       'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}})
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--entry',required=True);parser.add_argument('--output',default='f5_coupled_page_read_20261018');args=parser.parse_args()
    assert Path(args.output).name==args.output
    entry=ROOT/'results'/args.entry
    assert read(entry/'entry_decision.json')['timing_allowed']
    identity_entry=read(entry/'identity.json')
    for path,digest in identity_entry['sources'].items():assert sha(WORKSPACE/path)==digest,('entry drift',path)
    sources=dict(identity_entry['sources'])
    for path in (Path(__file__),ROOT/'diagnostics/bench_coupled_page_read.cpp',ROOT/'diagnostics/prepare_coupled_runners.py'):
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    inputs=identity_entry['inputs']
    for item in inputs.values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    identity={'sources':sources,'inputs':inputs,'commands':[],'artifacts':{},'cpu':min(os.sched_getaffinity(0)),
              'entry_identity_sha256':sha(entry/'identity.json'),'entry_path':str(entry.relative_to(WORKSPACE)),
              'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0]}
    def save(name,obj):(out/name).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
    for path in sources:
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    save('identity.json',identity);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));identity['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,timeout=600,env=env)
        log.write(r.stdout+r.stderr);log.flush()
        if r.returncode:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});save('identity.json',identity)
            raise RuntimeError('F5 read entry failed; see failure.json')
        assert not r.stderr;return [json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')]
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),'-I'+str(ROOT/'learned-index/src')]
    driver=build/'coupled_read';san=build/'coupled_read_san'
    for exe,opts in [(driver,['-O3']),(san,['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        print('compiling',exe.name,flush=True)
        run(['g++']+flags+opts+[ROOT/'diagnostics/bench_coupled_page_read.cpp',ROOT/'learned-index/src/unified/runner.cpp','-o',exe])
        identity['artifacts'][str(exe.relative_to(out))]=sha(exe)
    def command(exe,item,state,process,mode):
        return ['taskset','-c',identity['cpu'],exe,WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path'],state,process,mode]
    gates=[];raw=[]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    for name,item in inputs.items():
        for state in ('clean','dirty'):
            normal=run(command(driver,item,state,0,'gate'))
            sanitized=run(command(san,item,state,0,'gate'),env);assert normal==sanitized and len(normal)==1
            gate=normal[0];assert gate['inplace_updates']==gate['writes'] and gate['rebuilds']==0
            gates.append({'cell':name+'/'+state,**gate});save('gates.json',gates)
            print('sanitized gate',name,state,gate['checks'],flush=True)
    for name,item in inputs.items():
        for state in ('clean','dirty'):
            cell=name+'/'+state;expected=next(g for g in gates if g['cell']==cell)
            for process in range(5):
                lines=run(command(driver,item,state,process,'timing'));assert len(lines)==33
                assert lines[0]=={k:v for k,v in expected.items() if k!='cell'}
                for row in lines[1:]:
                    assert row['operations']==item['queries']['count'] and row['elapsed_ns']>0
                    row.update(cell=cell,process=process);raw.append(row)
                save('raw.json',raw)
            print('timing',cell,'x5 complete',flush=True)
    assert len(raw)==960
    summary=summarize(raw);save('summary.json',summary)
    failed=[r['cell'] for r in summary if r['read_budget_failed']]
    save('decision.json',{'read_budget_failed_cells':failed,'production_adopted':False,'full_dynamic_matrix_started':False,
                          'unresolved_cells':[r['cell'] for r in summary if r['read_budget_status']=='unresolved'],
                          'dynamic_gate_allowed':all(r['read_budget_status']=='within budget' for r in summary),
                          'status':'close current layout at read-budget gate' if failed else 'read screen complete; adoption and full dynamic matrix pending'})
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    for item in inputs.values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    save('identity.json',identity);log.close()
    for r in summary:print(r['cell'],'live/D1',round(r['ratios']['live/D1']['median'],3),'failed',r['read_budget_failed'],flush=True)


if __name__=='__main__':main()

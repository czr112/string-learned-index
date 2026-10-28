"""F3 six-arm read entry, conditional on semantic and space gates."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
ARMS=['live','live_AA','control','control_AA','D1','D1_AA']


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def summarize(raw):
    result=[]
    for cell in sorted({r['cell'] for r in raw}):
        rows=[r for r in raw if r['cell']==cell];assert len(rows)==180
        values={name:[] for name in ('live/control','live/D1','control/D1','live_AA/live','control_AA/control','D1_AA/D1')}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==36
            table={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(table)==36
            for repeat in range(6):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==set(ARMS)
                assert len({r['checksum'] for r in seq})==1
            for name in values:
                a,b=name.split('/');values[name].append(statistics.median(table[r,a]/table[r,b] for r in range(6)))
        bands={arm:max(max(x,1/x) for x in values[arm+'_AA/'+arm]) for arm in ('live','control','D1')}
        ratios={}
        for name,points in values.items():
            a,b=name.split('/');band=max(bands.get(a.replace('_AA',''),1),bands.get(b,1))
            median=statistics.median(points)
            ratios[name]={'median':median,'process_medians':points,'slower':sum(x>1 for x in points),'band':band,
                          'clear_regression_5pct':median>1.05 and sum(x>1 for x in points)>=4 and median>band}
        r=ratios['live/D1']
        result.append({'cell':cell,'ratios':ratios,'aa_bands':bands,
                       'read_budget_failed':r['median']>1.15 and r['slower']>=4 and r['median']>r['band'],
                       'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}})
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--entry',required=True);parser.add_argument('--output',default='f3_slotted_page_read_20261018');args=parser.parse_args()
    assert Path(args.output).name==args.output
    entry=ROOT/'results'/args.entry
    assert read(entry/'entry_decision.json')['timing_allowed']
    identity_entry=read(entry/'identity.json')
    for path,digest in identity_entry['sources'].items():assert sha(WORKSPACE/path)==digest,('entry drift',path)
    sources=dict(identity_entry['sources'])
    for path in (Path(__file__),ROOT/'diagnostics/bench_slotted_page_read.cpp'):
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
    save('identity.json',identity);log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));identity['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,timeout=600,env=env)
        log.write(r.stdout+r.stderr);log.flush()
        if r.returncode:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});save('identity.json',identity)
            raise RuntimeError('F3 read entry failed; see failure.json')
        assert not r.stderr;return [json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')]
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),'-I'+str(ROOT/'learned-index/src')]
    driver=build/'slotted_read';san=build/'slotted_read_san'
    for exe,opts in [(driver,['-O3']),(san,['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        print('compiling',exe.name,flush=True)
        run(['g++']+flags+opts+[ROOT/'diagnostics/bench_slotted_page_read.cpp',ROOT/'learned-index/src/unified/runner.cpp','-o',exe])
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
                lines=run(command(driver,item,state,process,'timing'));assert len(lines)==37
                assert lines[0]=={k:v for k,v in expected.items() if k!='cell'}
                for row in lines[1:]:
                    assert row['operations']==item['queries']['count'] and row['elapsed_ns']>0
                    row.update(cell=cell,process=process);raw.append(row)
                save('raw.json',raw)
            print('timing',cell,'x5 complete',flush=True)
    assert len(raw)==1080
    summary=summarize(raw);save('summary.json',summary)
    failed=[r['cell'] for r in summary if r['read_budget_failed']]
    save('decision.json',{'read_budget_failed_cells':failed,'production_adopted':False,'full_dynamic_matrix_started':False,
                          'status':'close current layout at read-budget gate' if failed else 'not rejected by read screen; full dynamic validation pending'})
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    for item in inputs.values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    save('identity.json',identity);log.close()
    for r in summary:print(r['cell'],'live/control',round(r['ratios']['live/control']['median'],3),'live/D1',round(r['ratios']['live/D1']['median'],3),'failed',r['read_budget_failed'],flush=True)


if __name__=='__main__':main()

"""F4 same-leaf 2x2: actual root lookup and complete page verification."""
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
BASE=['tree_wrapped','tree_single','flat_wrapped','flat_single']
ARMS=[a for base in BASE for a in (base,base+'_AA')]
CELLS=[('tranco64_norepl','clean'),('tranco64_norepl','dirty'),('mixed_full_norepl','clean'),('skew_full_norepl','clean')]
CONTRASTS=['flat_wrapped/tree_wrapped','tree_single/tree_wrapped','flat_single/flat_wrapped','flat_single/tree_wrapped']


def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(raw):
    result=[]
    for name,state in CELLS:
        cell=name+'/'+state;rows=[r for r in raw if r['cell']==cell];assert len(rows)==320
        names=CONTRASTS+[a+'_AA/'+a for a in BASE];values={k:[] for k in names}
        for process in range(5):
            block=[r for r in rows if r['process']==process];assert len(block)==64
            table={(r['repeat'],r['arm']):r['elapsed_ns'] for r in block};assert len(table)==64
            for repeat in range(8):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(8)) and {r['arm'] for r in seq}==set(ARMS)
                assert len({r['checksum'] for r in seq})==1
            for key in names:
                a,b=key.split('/');values[key].append(statistics.median(table[r,a]/table[r,b] for r in range(8)))
        bands={a:max(max(x,1/x) for x in values[a+'_AA/'+a]) for a in BASE};ratios={}
        for key,points in values.items():
            a,b=key.split('/');band=max(bands[a.replace('_AA','')],bands[b]);median=statistics.median(points)
            faster=sum(x<1 for x in points);slower=sum(x>1 for x in points)
            ratios[key]={'process_medians':points,'median':median,'faster':faster,'slower':slower,'band':band,
                         'clear_gain_10pct':median<=.90 and faster>=4 and 1/median>band,
                         'clear_regression_5pct':median>1.05 and slower>=4 and median>band}
        result.append({'cell':cell,'ratios':ratios,'aa_bands':bands,
                       'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}})
    return result


def decide(summary):
    decisions={}
    for mechanism,contrast in [('root','flat_wrapped/tree_wrapped'),('single_page','tree_single/tree_wrapped'),('combined','flat_single/tree_wrapped')]:
        main=[r for r in summary if r['cell'].startswith('tranco64')];risks=[r for r in summary if not r['cell'].startswith('tranco64')]
        decisions[mechanism]={'both_main_gains':all(r['ratios'][contrast]['clear_gain_10pct'] for r in main),
                              'risk_regressions':[r['cell'] for r in risks if r['ratios'][contrast]['clear_regression_5pct']]}
        decisions[mechanism]['entry_passed']=decisions[mechanism]['both_main_gains'] and not decisions[mechanism]['risk_regressions']
    selected='root' if decisions['root']['entry_passed'] else 'single_page' if decisions['single_page']['entry_passed'] else None
    return {'mechanisms':decisions,'selected_single_mechanism':selected,'production_adopted':False,'dynamic_topology_validated':False,
            'status':'one mechanism eligible for a separately validated dynamic prototype' if selected else 'no independent mechanism passed; bounded attribution closed'}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='f4_route_attribution_20261018');args=parser.parse_args()
    assert Path(args.output).name==args.output
    f3=ROOT/'results/f3_slotted_page_read_20261018';frozen=read(f3/'identity.json')
    for path,digest in frozen['sources'].items():assert sha(WORKSPACE/path)==digest,('frozen F3 drift',path)
    sources=dict(frozen['sources'])
    for path in [Path(__file__),*[ROOT/'diagnostics'/name for name in ('F4_ROUTE_ATTRIBUTION_SPEC_20261018.md','generate_route_attribution.py',
                 'route_attribution_page.hpp','route_attribution_index.hpp','route_attribution.hpp','bench_route_attribution.cpp')]]:
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    inputs=frozen['inputs']
    for item in inputs.values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'sources':sources,'inputs':inputs,'commands':[],'artifacts':{},'cpu':min(os.sched_getaffinity(0)),
          'f3_identity_sha256':sha(f3/'identity.json'),'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0]}
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
            raise RuntimeError('F4 execution failed; see failure.json')
        assert not r.stderr;return [json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')]
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'),'-I'+str(ROOT/'learned-index/src')]
    driver=build/'route_audit';san=build/'route_audit_san'
    for exe,options in [(driver,['-O3']),(san,['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        print('compiling',exe.name,flush=True)
        run(['g++']+flags+options+[ROOT/'diagnostics/bench_route_attribution.cpp',ROOT/'learned-index/src/unified/runner.cpp','-o',exe])
        meta['artifacts'][str(exe.relative_to(out))]=sha(exe)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    ordinary=run([driver,'domain']);sanitized=run([san,'domain'],env)
    assert ordinary==sanitized and ordinary[0]['status']=='passed';save('domain.json',{'normal':ordinary,'sanitized':sanitized})
    print('domain normal/sanitized',ordinary[0]['checks'],'checks',flush=True)
    def command(exe,name,state,process,mode):
        item=inputs[name];return ['taskset','-c',meta['cpu'],exe,WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path'],state,process,mode]
    gates=[];raw=[]
    for name,state in CELLS:
        normal=run(command(driver,name,state,0,'gate'));sanitized=run(command(san,name,state,0,'gate'),env)
        assert normal==sanitized and len(normal)==1 and normal[0]['rebuilds']==0
        gates.append({'cell':name+'/'+state,**normal[0]});save('gates.json',gates)
        print('sanitized gate',name,state,normal[0]['checks'],flush=True)
    for name,state in CELLS:
        cell=name+'/'+state;expected=next(g for g in gates if g['cell']==cell)
        for process in range(5):
            lines=run(command(driver,name,state,process,'timing'));assert len(lines)==65
            assert lines[0]=={k:v for k,v in expected.items() if k!='cell'}
            for row in lines[1:]:
                assert row['operations']==inputs[name]['queries']['count'] and row['elapsed_ns']>0
                row.update(cell=cell,process=process);raw.append(row)
            save('raw.json',raw)
        print('paired 2x2',cell,'x5 complete',flush=True)
    assert len(raw)==1280
    summary=summarize(raw);save('summary.json',summary);save('decision.json',decide(summary))
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    for item in inputs.values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    save('identity.json',meta);log.close()
    for cell in summary:print(cell['cell'],{k:round(cell['ratios'][k]['median'],3) for k in CONTRASTS},flush=True)
    print(json.dumps(decide(summary)),flush=True)


if __name__=='__main__':main()

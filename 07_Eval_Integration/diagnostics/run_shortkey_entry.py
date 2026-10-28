"""C2 entry: work ledger over the complete D1 query path plus a phase decomposition."""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
UPSTREAM=WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'
E2=ROOT/'results/e2_generalization_20261018'
CELLS=['skew_full_norepl','mixed_full_norepl','tranco64_norepl','skew_miss','tranco64_miss']
PHASE_CELLS=['tranco64_norepl','tranco64_miss','mixed_full_norepl','skew_full_norepl']
SAN_CELLS=['tranco64_norepl','skew_full_norepl']
ARMS=['region','window','window_AA','full','full_AA','directory','ART','HOT']


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def save(p,obj):p.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n')


def verify_process(rows,expected):
    assert len(rows)==48
    assert all(r['operations']==expected and r['elapsed_ns']>0 for r in rows)
    for repeat in range(6):
        seq=sorted((r for r in rows if r['repeat']==repeat),key=lambda r:r['position'])
        assert [r['position'] for r in seq]==list(range(8))
        assert {r['arm'] for r in seq}==set(ARMS)
        assert len({r['checksum'] for r in seq if r['arm'] in ('full','full_AA')})==1
        assert len({r['checksum'] for r in seq if r['arm'] in ('window','window_AA')})==1
    for a in ARMS:assert sum(1 for r in rows if r['arm']==a)==6


def summary(raw):
    cells=[]
    for cell in PHASE_CELLS:
        rows=[r for r in raw if r['cell']==cell]
        assert len(rows)==240
        ns={a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS}
        ratios={}
        for arm in ARMS:
            if arm=='full':continue
            per=[]
            for process in range(5):
                t={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                per.append(statistics.median(t[rep,arm]/t[rep,'full'] for rep in range(6)))
            ratios[arm+'/full']={'process_medians':per,'median':statistics.median(per),
                'faster':sum(x<1 for x in per),'slower':sum(x>1 for x in per)}
        bands={}
        bands['full_AA/full']=max(max(x,1/x) for x in ratios['full_AA/full']['process_medians'])
        aa_window=[]
        for process in range(5):
            t={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
            aa_window.append(statistics.median(t[rep,'window_AA']/t[rep,'window'] for rep in range(6)))
        bands['window_AA/window']=max(max(x,1/x) for x in aa_window)
        ratios['window_AA/window']={'process_medians':aa_window,'median':statistics.median(aa_window),
            'faster':sum(x<1 for x in aa_window),'slower':sum(x>1 for x in aa_window)}
        band=max(bands.values())
        for name,r in ratios.items():
            r['clear_gain']=r['median']<=.95 and r['faster']>=4 and 1/r['median']>band
            r['clear_regression']=r['median']>1.05 and r['slower']>=4 and r['median']>band
        cells.append({'cell':cell,'ns_per_op':ns,'ratio_to_full':ratios,'aa_bands':bands,'observed_band':band})
    return cells


def decision(cells,ledger):
    main=next(c for c in cells if c['cell']=='tranco64_norepl')
    r=main['ratio_to_full']
    nav=r['region/full']['median']
    window=r['window/full']['median']
    directory=r['directory/full']['median']
    art=r['ART/full']['median']
    band=main['observed_band']
    nav_share=1-nav
    window_share=window-nav
    verify_share=1-window
    return {'tranco64_ns_per_op':main['ns_per_op'],
        'tranco64_nav_share':nav_share,'tranco64_window_selection_share':window_share,
        'tranco64_verify_share':verify_share,
        'tranco64_directory_over_full':directory,'tranco64_ART_over_full':art,
        'observed_band':band,
        'region_clearly_below_full':r['region/full']['clear_gain'],
        'exact_positioning_is_faster':directory<0.95 and r['directory/full']['clear_gain'],
        'root_navigation_prototype_justified':nav_share>=0.45 and nav_share>band
            and not (directory<0.95 and r['directory/full']['clear_gain']),
        'model_parameter_search_allowed':False,'candidate_built':False,'production_adopted':False,
        'ledger_cells':ledger,'status':'entry-only; no candidate, no default change'}


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='c2_shortkey_entry_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output
    e2=read(E2/'identity.json');frozen=read(ROOT/'results/collision_refinement_20261018b/identity.json')
    package=ROOT/'adapters/our_static_refined';manifest=read(package/'benchmark-adapter.json')
    identity=manifest['source_identity']
    for item in identity['core_sources']:
        assert sha(WORKSPACE/item['path'])==item['sha256']==frozen['sources'][item['path']]
    bundle=hashlib.sha256(''.join(x['sha256'] for x in identity['core_sources']).encode()).hexdigest()
    assert bundle==e2['descriptor']['core_bundle']
    for name,h in e2['artifacts'].items():
        if name.endswith('.a'):assert sha(E2/name)==h,('E2 library drift',name)
    libs=[E2/'build/our_static_refined/libour_static_refined_adapter.a',E2/'build/art/libart_adapter.a',
          E2/'build/hot/libhot_adapter.a']
    for lib in libs:assert lib.is_file()

    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'cpu':min(os.sched_getaffinity(0)),'sources':{},'inputs':{},'artifacts':{},'commands':[],
        'descriptor':{'core_bundle':bundle,'diagnostic_copy':sha(ROOT/'diagnostics/shortkey_entry_index.hpp'),
                      'driver':sha(ROOT/'diagnostics/bench_shortkey_entry.cpp')},
        'reused_libraries':{str(x.relative_to(E2)):sha(x) for x in libs},
        'protocol':'ledger=one counted pass per cell; phase=5 processes x 6 Williams rows, '
                   'region/window/full/directory/ART/HOT, one permutation per repeat',
        'limits':['counts are not timings','phase difference is an upper bound on removable work',
                  'no A/A arm for the phase arms','not a candidate round','no default change']}
    files={Path(__file__),ROOT/'diagnostics/verify_shortkey_entry.py',ROOT/'diagnostics/C2_SHORTKEY_ENTRY_SPEC_20261018.md',
           ROOT/'diagnostics/bench_shortkey_entry.cpp',ROOT/'diagnostics/shortkey_entry_index.hpp',
           ROOT/'diagnostics/collision_refined_index.hpp',ROOT/'diagnostics/collision_refinement.hpp',
           UPSTREAM/'static_physical_index.hpp',UPSTREAM/'physical_locator_index.hpp'}
    log=(out/'commands.log').open('x')
    def run(command,env=None,timeout=1800):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=timeout,env=env)
        log.write(r.stdout);log.write(r.stderr);log.flush()
        if r.returncode:
            save(out/'failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr[-12000:]})
            save(out/'identity.json',meta);raise RuntimeError('command failed; see failure.json')
        return r
    meta['compiler']=run(['g++','--version']).stdout.splitlines()[0]

    inputs={}
    for cell in CELLS:
        item=e2['inputs'][cell]
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
        inputs[cell]={'rows_path':item['rows_path'],'rows_sha256':item['rows_sha256'],'rows':item['rows'],
                      'queries_path':item['queries_path'],'queries_sha256':item['queries']['sha256'],
                      'queries':item['queries']['count'],'kind':item['queries']['kind']}
    meta['inputs']=inputs
    save(out/'identity.json',meta)

    flags=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Idiagnostics',
           '-Iadapters/art','-Iadapters/hot']
    def compile_exe(name,opt,extra):
        objects=[]
        for i,source in enumerate(['diagnostics/bench_shortkey_entry.cpp','learned-index/src/unified/runner.cpp']):
            obj=build/(name+str(i)+'.o');dep=obj.with_suffix('.d')
            run(['g++']+flags+opt+extra+['-MMD','-MF',dep,'-c',source,'-o',obj]);objects.append(obj)
        exe=build/name
        run(['g++','-march=x86-64-v3']+opt+extra+objects+libs+['-o',exe])
        meta['artifacts'][str(exe.relative_to(out))]=sha(exe);return exe
    print('compiling C2 entry driver',flush=True)
    driver=compile_exe('c2entry',['-O3'],[])
    sanitized=compile_exe('c2entry_san',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],[])

    def args_for(cell):
        item=inputs[cell];return [WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
    ledger=[];san_ledger=[]
    for cell in CELLS:
        r=run(['taskset','-c',meta['cpu'],driver]+args_for(cell)+['ledger',0]);assert not r.stderr
        record=json.loads(r.stdout);record['cell']=cell;ledger.append(record);save(out/'ledger.json',ledger)
        print('ledger',cell,'checked',record['checked_keys'],'keys',flush=True)
    for cell in SAN_CELLS:
        r=run(['taskset','-c',meta['cpu'],sanitized]+args_for(cell)+['ledger',0],
              env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert not r.stderr
        record=json.loads(r.stdout);record['cell']=cell;san_ledger.append(record);save(out/'ledger_sanitized.json',san_ledger)
        normal=next(x for x in ledger if x['cell']==cell)
        assert record['checked_keys']==normal['checked_keys'] and record['locations']==normal['locations']
        assert record['counter']['window_blocks']['total']==normal['counter']['window_blocks']['total']
        assert record['counter']['tag_block_loads']['total']==normal['counter']['tag_block_loads']['total']
        print('sanitized ledger',cell,'matches',flush=True)

    raw=[]
    for cell in PHASE_CELLS:
        for process in range(5):
            r=run(['taskset','-c',meta['cpu'],driver]+args_for(cell)+['phase',process]);assert not r.stderr
            records=[json.loads(line) for line in r.stdout.splitlines()]
            gate=[x for x in records if x['phase']=='gate'];assert len(gate)==1
            passes=[x for x in records if x['phase']=='pass']
            verify_process(passes,inputs[cell]['queries'])
            for row in passes:row.update(cell=cell,process=process)
            raw.extend(passes);save(out/'raw.json',raw)
        print('phase',cell,'x5 done',flush=True)
    assert len(raw)==4*5*48

    for cell,item in meta['inputs'].items():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries_sha256']
    meta['sources']={str(x.relative_to(WORKSPACE)):sha(x) for x in sorted(files)}
    cells=summary(raw)
    save(out/'summary.json',cells)
    save(out/'decision.json',decision(cells,[c['cell'] for c in ledger]))
    save(out/'identity.json',meta)
    log.close();data=(out/'commands.log').read_bytes()
    with (out/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(data)
    assert gzip.decompress((out/'commands.log.gz').read_bytes())==data
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),
                                 'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink()
    print('completed',len(raw),'timing rows',len(ledger),'ledger cells',flush=True)


if __name__=='__main__':main()

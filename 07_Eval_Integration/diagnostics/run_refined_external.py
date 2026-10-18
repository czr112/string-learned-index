"""E1: fresh formal packages, full gates, then one frozen external matrix."""
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
ARMS=['prototype','prototype_AA','integrated','integrated_AA','ART','HOT']
PAIRS=[('integrated','prototype'),('integrated','ART'),('integrated','HOT'),('prototype','ART'),
       ('prototype','HOT'),('prototype_AA','prototype'),('integrated_AA','integrated')]
CELLS=[f'{c}_{s}_{n}' for c,s in [('skew','full'),('mixed','full'),('tranco','full'),('tranco','64k')] for n in [10000,100000]]
GATE_CELLS=[f'{c}_{s}_100000' for c,s in [('tranco','full'),('tranco','64k'),('mixed','full'),('skew','full')]]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def save(p,obj):p.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n')


def verify_process(rows):
    assert len(rows)==36 and len({r['checksum'] for r in rows})==1
    assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in rows)
    pairs=collections.Counter()
    for repeat in range(6):
        seq=sorted((r for r in rows if r['repeat']==repeat),key=lambda r:r['position'])
        assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==set(ARMS)
        for a,b in zip(seq,seq[1:]):pairs[a['arm'],b['arm']]+=1
    assert len(pairs)==30 and set(pairs.values())=={1}
    for a in ARMS:assert sorted(r['position'] for r in rows if r['arm']==a)==list(range(6))


def summary(raw,heads):
    result=[]
    for cell in CELLS:
        rows=[r for r in raw if r['cell']==cell]
        if len(rows)!=180:continue
        ratios={}
        for a,b in PAIRS:
            medians=[]
            for process in range(5):
                times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                medians.append(statistics.median(times[rep,a]/times[rep,b] for rep in range(6)))
            ratios[a+'/'+b]={'process_medians':medians,'median':statistics.median(medians),
                'faster':sum(x<1 for x in medians),'slower':sum(x>1 for x in medians)}
        bands={name:max(max(x,1/x) for x in ratios[name]['process_medians'])
               for name in ['prototype_AA/prototype','integrated_AA/integrated']}
        f=max(bands.values())
        for r in ratios.values():
            r['clear_gain']=r['median']<=.95 and r['faster']>=4 and 1/r['median']>f
            r['clear_regression']=r['median']>1.05 and r['slower']>=4 and r['median']>f
        builds={a:statistics.median(g['build_ns'] for g in heads if g['cell']==cell and g['arm']==a)/1e6
                for a in ['prototype','integrated','ART','HOT']}
        memory={a:[{k:g[k] for k in ['index_bytes','adapter_bytes','retained_input_bytes']}
                   for g in heads if g['cell']==cell and g['arm']==a] for a in builds}
        result.append({'cell':cell,'ratios':ratios,'aa_bands':bands,'observed_band':f,
            'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS},
            'build_ms_descriptive':builds,'memory_by_process':memory})
    return result


def decision(cells):
    ratios={c['cell']:c['ratios']['integrated/ART']['median'] for c in cells}
    qualifying=[c for c,r in ratios.items() if r<=1]
    return {'integration_regression_cells':[c['cell'] for c in cells if c['ratios']['integrated/prototype']['clear_regression']],
        'ART_within_10_percent_all_cells':all(r<=1.10 for r in ratios.values()),
        'advanced_target_met':len(qualifying)>=2 and any(c in qualifying for c in ['tranco_64k_10000','tranco_64k_100000','mixed_full_10000']),
        'ART_at_or_below_one_cells':qualifying,'ART_HOT_retested':True,'production_adopted':False,
        'generalization_pending':True,'original_S3_failure_preserved':True}


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='refined_external_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output
    prior=read(ROOT/'results/static_interface_20261012c/identity.json')
    frozen=read(ROOT/'results/collision_refinement_20261018b/identity.json')
    package=ROOT/'adapters/our_static_refined';manifest=read(package/'benchmark-adapter.json');identity=manifest['source_identity']
    for item in identity['core_sources']:
        assert sha(WORKSPACE/item['path'])==item['sha256']==frozen['sources'][item['path']]
    bundle=hashlib.sha256(''.join(x['sha256'] for x in identity['core_sources']).encode()).hexdigest()
    adapter=hashlib.sha256((sha(package/'our_static_refined_adapter.cpp')+sha(package/'our_static_refined_adapter.h')).encode()).hexdigest()
    assert bundle==identity['core_bundle_sha256'] and adapter==identity['adapter_digest']
    prototype=ROOT/'diagnostics/collision_refinement_adapter.hpp'
    assert sha(prototype)==frozen['sources'][str(prototype.relative_to(WORKSPACE))]
    old_bundle=hashlib.sha256(''.join(x['sha256'] for x in identity['core_sources'][:2]).encode()).hexdigest()
    art_manifest=read(ROOT/'adapters/art/benchmark-adapter.json')
    hot_manifest=read(ROOT/'adapters/hot/benchmark-adapter.json')
    art_src=UPSTREAM/'third_party/vendor/libart/src'
    assert sha(art_src/'art.c')=='b65f20fdc160a6e4c0b1b390086f682d381e3638d8690594158ad75cda33396e'
    assert sha(art_src/'art.h')=='4443de6fd42581982167f7826f63bd956d21a958f204575e52512e007deab5af'
    hot=ROOT/'hot-src'
    hot_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=hot,text=True).strip()
    assert hot_commit=='96bf6fb7103b27e50e16a6026db8974c090ee84a'
    assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=hot,text=True).strip()
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    meta={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'cpu':min(os.sched_getaffinity(0)),'sources':{},'inputs':{},'artifacts':{},'commands':[],
        'descriptor':{'core_bundle':bundle,'adapter':adapter,'prototype_bundle':old_bundle,'prototype_adapter':sha(prototype)},
        'HOT_commit':hot_commit,'protocol':'5 processes x 6 Williams rows; 200k immediate warmup + 200k timed; paired per repeat',
        'limits':['ART allocator bytes unknown','build order descriptive only','generalization pending','not production default']}
    files={Path(__file__),ROOT/'diagnostics/verify_refined_external.py',ROOT/'diagnostics/REFINED_EXTERNAL_SPEC_20261018.md',
           ROOT/'adapters/common/contract_acceptance.cpp',art_src/'art.c',art_src/'art.h'}
    for name in ['our_static_refined','art','hot']:
        files.update([ROOT/'adapters'/name/'CMakeLists.txt',ROOT/'adapters'/name/'benchmark-adapter.json'])
    log=(out/'commands.log').open('x')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=900,env=env)
        log.write(r.stdout);log.write(r.stderr);log.flush()
        if r.returncode:
            save(out/'failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr[-12000:]});save(out/'identity.json',meta)
            raise RuntimeError('command failed; see failure.json')
        return r
    meta['compiler']=run(['g++','--version']).stdout.splitlines()[0];assert meta['compiler']==prior['compiler']
    meta['C_compiler']=run(['gcc','--version']).stdout.splitlines()[0]
    meta['cmake']=run(['cmake','--version']).stdout.splitlines()[0]
    def deps(dep):
        for value in dep.read_text().replace('\\\n',' ').split(':',1)[1].split():
            source=(ROOT/value).resolve()
            if source.is_file() and source.is_relative_to(WORKSPACE) and not source.is_relative_to(build):files.add(source)
    def artifact(path):meta['artifacts'][str(path.relative_to(out))]=sha(path)
    libs=[]
    for folder,opt,target,accept in [('our_static_refined','REFINED','our_static_refined_adapter','our_static_refined_acceptance'),
            ('art','ART','art_adapter','art_adapter_acceptance'),('hot','HOT','hot_adapter','hot_adapter_acceptance')]:
        dest=build/folder
        print('fresh CMake package',folder,flush=True)
        run(['cmake','-S',ROOT/'adapters'/folder,'-B',dest,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
             '-DCMAKE_CXX_STANDARD=17','-DCMAKE_CXX_EXTENSIONS=OFF','-DCMAKE_C_STANDARD=99','-DCMAKE_C_EXTENSIONS=OFF',
             '-D'+opt+'_BUILD_ACCEPTANCE=ON'])
        run(['cmake','--build',dest,'--parallel','2'])
        commands=read(dest/'compile_commands.json')
        assert commands and all('-O3' in c['command'] and '-march=x86-64-v3' in c['command'] and '-DNDEBUG' not in c['command'] for c in commands)
        for dep in dest.rglob('*.o.d'):deps(dep)
        lib=dest/('lib'+target+'.a');libs.append(lib);artifact(lib);artifact(dest/accept);artifact(dest/'compile_commands.json')
        result=run([dest/accept]);assert not result.stderr
        (out/(folder+'_acceptance.txt')).write_text(result.stdout)
        print(result.stdout.splitlines()[-1],flush=True)
    # Exercise the evaluator's package discovery/build/runner, without ranking its tiny smoke run.
    (out/'smoke_keys.txt').write_text('alpha\nbeta\ndelta\nepsilon\ngamma\n')
    save(out/'smoke_workload.json',{'schema':'static-uniform-point/v1','seed':42,'operations':100,'miss_ratio':'0.1','repeats':1,'latency_samples':0})
    run(['python3','-B','learned-index/scripts/benchmark.py','--dataset',out/'smoke_keys.txt','--implementation',package,
         '--workload',out/'smoke_workload.json','--output',out/'discovery','--jobs','2'])
    report=read(out/'discovery/report.json');assert len(report['results'])==1 and report['results'][0]['status']=='passed'
    assert bundle in json.dumps(report['results']) and adapter in json.dumps(report['results'])
    artifact(out/'discovery/report.json');artifact(out/'smoke_keys.txt');artifact(out/'smoke_workload.json')
    files.add(ROOT/'learned-index/scripts/benchmark.py');files.update((ROOT/'learned-index/scripts/benchmark_lib').glob('*.py'))
    flags=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Idiagnostics','-Iadapters/our_static_refined',
        '-DREFINED_CORE_BUNDLE_SHA256="'+bundle+'"','-DREFINED_ADAPTER_SHA256="'+adapter+'"',
        '-DREFINEMENT_BASE_SHA256="'+sha(UPSTREAM/'physical_locator_index.hpp')+'"',
        '-DREFINEMENT_CORE_SHA256="'+old_bundle+'"','-DREFINEMENT_ADAPTER_SHA256="'+sha(prototype)+'"']
    def compile_exe(name,sources,opt,links):
        objects=[]
        for i,source in enumerate(sources):
            obj=build/(name+str(i)+'.o');dep=obj.with_suffix('.d')
            run(['g++']+flags+opt+['-MMD','-MF',dep,'-c',source,'-o',obj]);deps(dep);objects.append(obj)
        exe=build/name;run(['g++','-march=x86-64-v3']+opt+objects+links+['-o',exe]);artifact(exe);return exe
    print('compiling formal correctness gates and external driver',flush=True)
    sources=['diagnostics/check_refined_integration.cpp','learned-index/src/unified/runner.cpp']
    check=compile_exe('check',sources,['-O2'],[libs[0]])
    san=compile_exe('san_check',sources+['adapters/our_static_refined/our_static_refined_adapter.cpp'],
        ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],[])
    bench=compile_exe('bench',['diagnostics/bench_refined_external.cpp','learned-index/src/unified/runner.cpp'],['-O3'],libs)
    meta['sources']={str(x.relative_to(WORKSPACE)):sha(x) for x in sorted(files)}
    for cell in CELLS:
        item=prior['inputs'][cell];rows=WORKSPACE/item['rows_path'];queries=WORKSPACE/item['queries_path']
        assert sha(rows)==item['rows_sha256'] and sha(queries)==item['queries']['sha256']
        meta['inputs'][cell]={'rows_path':item['rows_path'],'queries_path':item['queries_path'],
            'rows_sha256':sha(rows),'queries_sha256':sha(queries),'rows':item['rows'],'queries':item['queries']['count']}
    save(out/'identity.json',meta);gates=[]
    for cell in GATE_CELLS:
        item=meta['inputs'][cell];args=[WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
        normal=run(['taskset','-c',meta['cpu'],check]+args);assert not normal.stderr;data=json.loads(normal.stdout)
        sanitized=run(['taskset','-c',meta['cpu'],san]+args,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert not sanitized.stderr;san_data=json.loads(sanitized.stdout)
        assert all(data[k]==san_data[k] for k in ['checks','domain_checks','probes','scans','rejections','work_checks','rows','queries'])
        for actual in [data,san_data]:
            assert actual['work_checks']==100000 and actual['refinement_bytes']<=.05*(actual['core_bytes']-actual['refinement_bytes'])
        gates.append({'cell':cell,'normal':data,'sanitized':san_data,'sanitizer_rc':0,'sanitizer_stderr_bytes':0})
        save(out/'gates.json',gates);print('formal gate',cell,data['checks'],'checks passed',flush=True)
    raw=[];heads=[]
    for cell in CELLS:
        item=meta['inputs'][cell];args=[WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
        for process in range(5):
            r=run(['taskset','-c',meta['cpu'],bench]+args+[process]);assert not r.stderr
            records=[json.loads(line) for line in r.stdout.splitlines()];assert len(records)==42
            h=records[:6];assert [g['arm'] for g in h]==ARMS
            assert all(g['phase']=='gate' and g['verified']==item['rows']+item['queries'] and g['retained_input_bytes']==0 for g in h)
            assert all(g['implementation_version']==bundle and g['adapter_version']==adapter for g in h[2:4])
            assert all(g['implementation_version']==old_bundle and g['adapter_version']==sha(prototype) for g in h[:2])
            assert '301046804af165269e37da6725f5a4aec9ecc881' in h[4]['implementation_version'] and hot_commit in h[5]['implementation_version']
            assert [g['object_id'] for g in h]==[0,0,1,1,2,3]
            assert h[0]['index_bytes']==h[2]['index_bytes']
            for g in h:heads.append(dict(g,cell=cell,process=process))
            save(out/'process_gates.json',heads)
            paired=records[6:];verify_process(paired)
            for row in paired:row.update(cell=cell,process=process)
            raw.extend(paired);save(out/'raw.json',raw);print('timing',cell,process+1,'/5 done',flush=True)
        save(out/'summary.json',summary(raw,heads))
    assert len(raw)==1440 and len(heads)==240
    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source drift',path)
    cells=summary(raw,heads);save(out/'summary.json',cells);save(out/'decision.json',decision(cells));save(out/'identity.json',meta)
    log.close();data=(out/'commands.log').read_bytes()
    with (out/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(data)
    assert gzip.decompress((out/'commands.log.gz').read_bytes())==data
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink();print('completed 1440 timing rows',flush=True)


if __name__=='__main__':main()

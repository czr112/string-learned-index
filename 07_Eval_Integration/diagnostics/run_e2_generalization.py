"""E2: no-replacement reads, 100%/0% hit endpoints, evicted first pass, and a space ledger."""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
UPSTREAM=WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'
ARMS=['prototype','prototype_AA','integrated','integrated_AA','ART','HOT']
PAIRS=[('integrated','prototype'),('integrated','ART'),('integrated','HOT'),('prototype','ART'),
       ('prototype','HOT'),('prototype_AA','prototype'),('integrated_AA','integrated')]
MODES=['warm','first','evicted']
SEED=20261018
DATASETS={
 'skew':{'rows':'07_Eval_Integration/results/matrix2_skew/interchange/rows.bin','corpus':'skew','scale':'full'},
 'mixed':{'rows':'07_Eval_Integration/results/matrix2_mixed/interchange/rows.bin','corpus':'mixed','scale':'full'},
 'tranco':{'rows':'07_Eval_Integration/results/matrix2_tranco/interchange/rows.bin','corpus':'tranco','scale':'full'},
 'tranco64':{'rows':'07_Eval_Integration/results/static_interface_20261012c/tranco_64k_rows.bin','corpus':'tranco','scale':'64k'},
}
CELLS=[('skew_full_norepl','skew','hit'),('mixed_full_norepl','mixed','hit'),('tranco64_norepl','tranco64','hit'),
       ('skew_miss','skew','miss'),('tranco64_miss','tranco64','miss')]
LEDGER_ARMS=['art','hot','integrated','prototype']


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def save(p,obj):p.write_text(json.dumps(obj,indent=2,ensure_ascii=False)+'\n')


def read_rows(path):
    data=path.read_bytes();assert data[:8]==b'SIBROW1\0'
    count,=struct.unpack_from('<Q',data,8);pos=16;rows=[]
    for _ in range(count):
        n,=struct.unpack_from('<Q',data,pos);pos+=8
        key=data[pos:pos+n];pos+=n;value,=struct.unpack_from('<Q',data,pos);pos+=8
        rows.append((key,value))
    assert pos==len(data) and all(rows[i-1][0]<rows[i][0] for i in range(1,len(rows)))
    return rows


def write_queries(path,items):
    with path.open('xb') as f:
        f.write(b'SIBQRY1\0'+struct.pack('<Q',len(items)))
        for key,value,present in items:f.write(struct.pack('<BQQ',present,value,len(key))+key)


def make_queries(path,rows,kind,seed):
    rng=random.Random(seed);items=[]
    if kind=='hit':
        items=[(key,value,True) for key,value in rows]
        rng.shuffle(items)
        hit=len(items);miss=0
    elif kind=='miss':
        reference={key for key,_ in rows};seen=set()
        for _ in range(len(rows)):
            base=rows[rng.randrange(len(rows))][0]
            key=base+b'~e2/'+str(rng.getrandbits(64)).encode('ascii')
            while key in reference or key in seen:key+=b'~'
            seen.add(key);items.append((key,0,False))
        rng.shuffle(items)
        hit=0;miss=len(items)
    else:raise ValueError(kind)
    write_queries(path,items)
    return {'kind':kind,'seed':seed,'count':len(items),'hit':hit,'miss':miss,
            'generator':'hit: every row key once, shuffled; miss: key + "~e2/" + 64-bit decimal, proved absent',
            'sha256':sha(path)}


def verify_process(rows,expected):
    assert len(rows)==36
    assert all(r['operations']==expected and r['elapsed_ns']>0 for r in rows)
    pairs=collections.Counter()
    for repeat in range(6):
        seq=sorted((r for r in rows if r['repeat']==repeat),key=lambda r:r['position'])
        assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==set(ARMS)
        # One permutation per repeat, so the six arms must agree inside a repeat. Different
        # repeats use different permutations and therefore carry different checksums.
        assert len({r['checksum'] for r in seq})==1,('arm checksum mismatch',repeat)
        for a,b in zip(seq,seq[1:]):pairs[a['arm'],b['arm']]+=1
    assert len(pairs)==30 and set(pairs.values())=={1}
    for a in ARMS:assert sorted(r['position'] for r in rows if r['arm']==a)==list(range(6))


def summary(raw,heads):
    result=[]
    for cell,dataset,kind in CELLS:
        for mode in MODES:
            rows=[r for r in raw if r['cell']==cell and r['mode']==mode]
            assert len(rows)==180,('incomplete cell',cell,mode,len(rows))
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
            memory={a:[{k:g[k] for k in ['index_bytes','adapter_bytes','retained_input_bytes']}
                       for g in heads if g['cell']==cell and g['arm']==a] for a in ['prototype','integrated','ART','HOT']}
            result.append({'cell':cell,'dataset':dataset,'kind':kind,'mode':mode,'ratios':ratios,
                'aa_bands':bands,'observed_band':f,
                'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==a) for a in ARMS},
                'memory_by_process':memory})
    return result


def decision(cells):
    warm={c['cell']:c for c in cells if c['mode']=='warm'}
    art={k:v['ratios']['integrated/ART']['median'] for k,v in warm.items()}
    hot={k:v['ratios']['integrated/HOT']['median'] for k,v in warm.items()}
    integration={k:v['ratios']['integrated/prototype']['median'] for k,v in warm.items()}
    return {
        'warm_full_library_ratios_to_ART':art,
        'warm_full_library_ratios_to_HOT':hot,
        'warm_integrated_vs_prototype':integration,
        'ART_within_10_percent_all_E2_cells':all(r<=1.10 for r in art.values()),
        'full_library_clear_gain_cells':[k for k,v in warm.items()
            if v['ratios']['integrated/ART']['clear_gain']],
        'clear_integration_regression_cells':[k for k,v in warm.items()
            if v['ratios']['integrated/prototype']['clear_regression']],
        'short_key_endpoint_ratios_to_ART':{k:v['ratios']['integrated/ART']['median']
            for k,v in warm.items() if k.startswith('tranco64')},
        'E1_general_gate_failure_preserved':True,
        'million_unique_keys_covered':False,
        'in_range_perturbation_and_boundary_misses_covered':False,
        'production_adopted':False,
        'dynamic_updates_implemented':False,
        'status':'evidence-only; explicit read-only candidate retained; F/G not started'}


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='e2_generalization_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output
    prior=read(ROOT/'results/static_interface_20261012c/identity.json')
    frozen=read(ROOT/'results/collision_refinement_20261018b/identity.json')
    package=ROOT/'adapters/our_static_refined';manifest=read(package/'benchmark-adapter.json')
    identity=manifest['source_identity']
    for item in identity['core_sources']:
        assert sha(WORKSPACE/item['path'])==item['sha256']==frozen['sources'][item['path']]
    bundle=hashlib.sha256(''.join(x['sha256'] for x in identity['core_sources']).encode()).hexdigest()
    adapter=hashlib.sha256((sha(package/'our_static_refined_adapter.cpp')+sha(package/'our_static_refined_adapter.h')).encode()).hexdigest()
    assert bundle==identity['core_bundle_sha256'] and adapter==identity['adapter_digest']
    prototype=ROOT/'diagnostics/collision_refinement_adapter.hpp'
    assert sha(prototype)==frozen['sources'][str(prototype.relative_to(WORKSPACE))]
    old_bundle=hashlib.sha256(''.join(x['sha256'] for x in identity['core_sources'][:2]).encode()).hexdigest()
    e1=read(ROOT/'results/refined_external_20261018/identity.json')
    assert e1['descriptor']=={'core_bundle':bundle,'adapter':adapter,'prototype_bundle':old_bundle,'prototype_adapter':sha(prototype)}
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
        'HOT_commit':hot_commit,
        'protocol':'5 processes; warm=1 untimed pass + 6 timed passes per arm, first=rebuild then one timed pass, '
                   'evicted=256 MiB write sweep then one timed pass; same deterministic permutation for all arms per repeat',
        'limits':['one query per row, hit or miss pass, no mixed ratio','eviction sweep is not a cold cache',
                  'million-key scale not covered','in-range perturbation and region-boundary misses not covered',
                  'wrapped allocation bytes attribute libart only','not production default']}
    files={Path(__file__),ROOT/'diagnostics/verify_e2_generalization.py',ROOT/'diagnostics/E2_GENERALIZATION_SPEC_20261018.md',
           ROOT/'diagnostics/bench_e2_generalization.cpp',ROOT/'diagnostics/ledger_e2_allocation.cpp',
           ROOT/'adapters/common/contract_acceptance.cpp',art_src/'art.c',art_src/'art.h'}
    for name in ['our_static_refined','art','hot']:
        files.update([ROOT/'adapters'/name/'CMakeLists.txt',ROOT/'adapters'/name/'benchmark-adapter.json'])
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

    # Workloads: proven against the frozen rows before any timing.
    inputs={}
    for cell,dataset,kind in CELLS:
        info=DATASETS[dataset];rows_path=WORKSPACE/info['rows']
        rows=read_rows(rows_path)
        expected_rows={'skew':131072,'mixed':131072,'tranco':262144,'tranco64':65536}[dataset]
        assert len(rows)==expected_rows,(cell,len(rows))
        q=out/(cell+'_queries.bin');qm=make_queries(q,rows,kind,SEED+len(cell))
        assert qm['count']==len(rows)
        inputs[cell]={'rows_path':info['rows'],'rows_sha256':sha(rows_path),'rows':len(rows),
            'queries_path':str(q.relative_to(WORKSPACE)),'queries':qm}
        print('workload',cell,qm['count'],'queries','hit' if qm['hit'] else 'miss',flush=True)
    # Bind the row sets to the S3 identity pins rather than trusting the paths.
    s3_rows={v['rows_path']:v['rows_sha256'] for v in prior['inputs'].values()}
    for item in inputs.values():assert s3_rows[item['rows_path']]==item['rows_sha256']
    meta['inputs']=inputs
    save(out/'identity.json',meta)

    flags=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Idiagnostics','-Iadapters/our_static_refined',
        '-DREFINED_CORE_BUNDLE_SHA256="'+bundle+'"','-DREFINED_ADAPTER_SHA256="'+adapter+'"',
        '-DREFINEMENT_BASE_SHA256="'+sha(UPSTREAM/'physical_locator_index.hpp')+'"',
        '-DREFINEMENT_CORE_SHA256="'+old_bundle+'"','-DREFINEMENT_ADAPTER_SHA256="'+sha(prototype)+'"']
    def compile_exe(name,sources,opt,links,link_flags=None):
        objects=[]
        for i,source in enumerate(sources):
            obj=build/(name+str(i)+'.o');dep=obj.with_suffix('.d')
            run(['g++']+flags+opt+['-MMD','-MF',dep,'-c',source,'-o',obj]);deps(dep);objects.append(obj)
        exe=build/name
        run(['g++','-march=x86-64-v3']+opt+objects+links+(link_flags or [])+['-o',exe]);artifact(exe);return exe

    print('compiling gates and E2 drivers',flush=True)
    gate_sources=['diagnostics/check_refined_integration.cpp','learned-index/src/unified/runner.cpp']
    check=compile_exe('check',gate_sources,['-O2'],[libs[0]])
    san=compile_exe('san_check',gate_sources+['adapters/our_static_refined/our_static_refined_adapter.cpp'],
        ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],[])
    bench=compile_exe('bench',['diagnostics/bench_e2_generalization.cpp','learned-index/src/unified/runner.cpp'],['-O3'],libs)
    ledger=compile_exe('ledger',['diagnostics/ledger_e2_allocation.cpp','learned-index/src/unified/runner.cpp'],['-O3'],libs,
        ['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free'])

    gates=[]
    for cell,dataset,kind in CELLS:
        item=meta['inputs'][cell];args=[WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
        normal=run(['taskset','-c',meta['cpu'],check]+args);assert not normal.stderr;data=json.loads(normal.stdout)
        sanitized=run(['taskset','-c',meta['cpu'],san]+args,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        assert not sanitized.stderr;san_data=json.loads(sanitized.stdout)
        assert data['checks']==san_data['checks'] and data['probes']==san_data['probes'] and data['rows']==item['rows']
        assert data['queries']==item['queries']['count']
        assert data['work_checks']==item['queries']['count'] and data['refinement_bytes']<=.05*(data['core_bytes']-data['refinement_bytes'])
        gates.append({'cell':cell,'normal':data,'sanitized':san_data,'sanitizer_rc':0,'sanitizer_stderr_bytes':0})
        save(out/'gates.json',gates);print('full gate',cell,data['checks'],'checks passed',flush=True)

    raw=[];heads=[]
    for cell,dataset,kind in CELLS:
        item=meta['inputs'][cell];args=[WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
        for process in range(5):
            r=run(['taskset','-c',meta['cpu'],bench]+args+['gate',process]);assert not r.stderr
            records=[json.loads(line) for line in r.stdout.splitlines()];assert len(records)==6
            assert [g['arm'] for g in records]==ARMS
            assert all(g['phase']=='gate' and g['verified']==item['rows']+item['queries']['count']
                and g['retained_input_bytes']==0 for g in records)
            assert all(g['implementation_version']==bundle and g['adapter_version']==adapter for g in records[2:4])
            assert all(g['implementation_version']==old_bundle and g['adapter_version']==sha(prototype) for g in records[:2])
            assert '301046804af165269e37da6725f5a4aec9ecc881' in records[4]['implementation_version']
            assert hot_commit in records[5]['implementation_version']
            assert [g['object_id'] for g in records]==[0,0,1,1,2,3]
            assert records[0]['index_bytes']==records[2]['index_bytes']
            for g in records:heads.append(dict(g,cell=cell,process=process))
            save(out/'process_gates.json',heads)
        save(out/'raw.json',raw)
        print('gate mode',cell,'x5 processes done',flush=True)
    for mode in MODES:
        for cell,dataset,kind in CELLS:
            item=meta['inputs'][cell];args=[WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path']]
            for process in range(5):
                command=['taskset','-c',meta['cpu'],bench]+args+[mode,process]
                if mode=='evicted':command.append('256')
                r=run(command);assert not r.stderr
                records=[json.loads(line) for line in r.stdout.splitlines()]
                assert len(records)==36 and all(g['mode']==mode for g in records)
                verify_process(records,item['queries']['count'])
                for row in records:row.update(cell=cell,process=process)
                raw.extend(records);save(out/'raw.json',raw)
            print('timing',mode,cell,'x5 processes done',flush=True)
    assert len(raw)==5*3*5*36 and len(heads)==5*5*6

    ledger_rows=[]
    for dataset,info in DATASETS.items():
        for arm in LEDGER_ARMS:
            r=run(['taskset','-c',meta['cpu'],ledger,WORKSPACE/info['rows'],arm]);assert not r.stderr
            record=json.loads(r.stdout);record['dataset']=dataset;record['rows_path']=info['rows']
            ledger_rows.append(record);save(out/'ledger.json',ledger_rows)
            print('ledger',dataset,arm,record['heap_inuse_delta']+record['heap_mmapped_delta'],'bytes heap',flush=True)

    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source drift',path)
    for cell,item in meta['inputs'].items():
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
    meta['sources']={str(x.relative_to(WORKSPACE)):sha(x) for x in sorted(files)}
    cells=summary(raw,heads)
    save(out/'summary.json',cells);save(out/'decision.json',decision(cells));save(out/'identity.json',meta)
    log.close();data=(out/'commands.log').read_bytes()
    with (out/'commands.log.gz').open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write(data)
    assert gzip.decompress((out/'commands.log.gz').read_bytes())==data
    save(out/'log_archive.json',{'raw_sha256':hashlib.sha256(data).hexdigest(),'gzip_sha256':sha(out/'commands.log.gz')})
    (out/'commands.log').unlink()
    print('completed',len(raw),'timing rows',len(heads),'gate rows',flush=True)


if __name__=='__main__':main()

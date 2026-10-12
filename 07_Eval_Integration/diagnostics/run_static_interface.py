"""S3 owning interface and one fixed wide-certificate fallback. Run under WSL."""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import statistics
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
UPSTREAM=WORKSPACE/'02_Code_and_Experiments/validation_e25_20260918'
ARMS=['prototype','learned','learned_AA','wide_directory','ART','HOT']

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,value): path.write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n')
def read_rows(path):
    data=path.read_bytes();assert data[:8]==b'SIBROW1\0'
    count,=struct.unpack_from('<Q',data,8);pos=16;rows=[]
    for _ in range(count):
        n,=struct.unpack_from('<Q',data,pos);pos+=8
        key=data[pos:pos+n];pos+=n;value,=struct.unpack_from('<Q',data,pos);pos+=8
        rows.append((key,value))
    assert pos==len(data) and all(rows[i-1][0]<rows[i][0] for i in range(1,len(rows)))
    return rows
def write_rows(path,rows):
    with path.open('xb') as f:
        f.write(b'SIBROW1\0'+struct.pack('<Q',len(rows)))
        for key,value in rows:f.write(struct.pack('<Q',len(key))+key+struct.pack('<Q',value))
def queries(path,rows,count):
    rng=random.Random(2026101202);reference=dict(rows);items=[]
    for i in range(count):
        key,value=rows[rng.randrange(len(rows))];present=i%10!=0
        if not present:
            key+=b'~static-s3/'+str(rng.getrandbits(64)).encode('ascii');value=0
            while key in reference:key+=b'~'
        assert (key in reference)==present and (not present or reference[key]==value)
        items.append((key,value,present))
    rng.shuffle(items)
    with path.open('xb') as f:
        f.write(b'SIBQRY1\0'+struct.pack('<Q',count))
        for key,value,present in items:f.write(struct.pack('<BQQ',present,value,len(key))+key)
    return {'seed':2026101202,'count':count,'hit':count*9//10,'miss':count//10,'sha256':digest(path)}

def verify_records(records):
    timings=[r for r in records if r['phase']=='paired']
    assert len(timings)==36 and len({r['checksum'] for r in timings})==1
    assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in timings)
    counts=collections.Counter()
    for rep in range(6):
        seq=sorted([r for r in timings if r['repeat']==rep],key=lambda r:r['position'])
        assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==set(ARMS)
        for a,b in zip(seq,seq[1:]):counts[a['arm'],b['arm']]+=1
    assert len(counts)==30 and set(counts.values())=={1}
    for a in ARMS:assert sorted(r['position'] for r in timings if r['arm']==a)==list(range(6))

def summarize(raw):
    cells=[]
    for cell in sorted({r['cell'] for r in raw}):
        records=[r for r in raw if r['cell']==cell and r['phase']=='paired'];assert len(records)==180
        values={}
        for a,b in [('learned','prototype'),('learned_AA','learned'),('wide_directory','learned'),('learned','ART'),('wide_directory','ART'),('wide_directory','HOT')]:
            ratios=[]
            for process in range(5):
                paired={(r['repeat'],r['arm']):r['elapsed_ns'] for r in records if r['process']==process}
                ratios.append(statistics.median(paired[rep,a]/paired[rep,b] for rep in range(6)))
            values[a+'/'+b]={'process_medians':ratios,'median':statistics.median(ratios),'faster':sum(x<1 for x in ratios),'slower':sum(x>1 for x in ratios)}
        band=max(max(x,1/x) for x in values['learned_AA/learned']['process_medians'])
        integration=values['learned/prototype'];hybrid=values['wide_directory/learned']
        cells.append({'cell':cell,'ratios':values,'aa_observed_band':band,
            'integration_regression':integration['median']>1.05 and integration['slower']>=4 and integration['median']>band,
            'candidate_gain':hybrid['median']<=.90 and hybrid['faster']>=4 and 1/hybrid['median']>band,
            'candidate_regression':hybrid['median']>1.05 and hybrid['slower']>=4 and hybrid['median']>band,
            'ns_per_op':{a:statistics.median(r['elapsed_ns']/r['operations'] for r in records if r['arm']==a) for a in ARMS}})
    return cells

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='static_interface_20261012')
    parser.add_argument('--reuse-gates',type=Path,help='completed S3 gate evidence, only if all gate sources/inputs/binaries match')
    args=parser.parse_args()
    if Path(args.output).name!=args.output:raise ValueError('output must be a directory name')
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False);build=out/'build';build.mkdir()
    previous=ROOT/'results/static_physical_20261012b';old=json.loads((previous/'identity.json').read_text())
    for path,sha in old['source_sha256'].items():assert digest(WORKSPACE/path)==sha,('frozen source drift',path)
    sources={WORKSPACE/path for path in old['source_sha256']}
    sources.update([Path(__file__),UPSTREAM/'static_physical_index.hpp',ROOT/'diagnostics/check_static_interface.cpp',
        ROOT/'diagnostics/bench_static_interface.cpp',ROOT/'diagnostics/verify_static_interface.py',ROOT/'adapters/common/contract_acceptance.cpp',
        WORKSPACE/'docs/NEXT_ACTION_PLAN_20261012_STATIC_S3.md'])
    sources.update((ROOT/'adapters/our_static').glob('*'))
    identity={str(p.relative_to(WORKSPACE)):digest(p) for p in sorted(sources) if p.is_file()}
    cpu=min(os.sched_getaffinity(0));meta={'sources':identity,'cpu':cpu,'commands':[],'inputs':{},'reused_objects':{},
        'protocol':'5 independent processes x 6 even Williams rows, immediate equal per-arm warm-up, same learned/A_A object; 200k operations per arm/repeat',
        'scope':'owning immutable interface; dynamic production untouched; learned default; wide_directory explicit; no LITS optimization',
        'limitations':['warm repeated queues, not cache-cold','ART total owned allocation unknown','capacity accounting excludes allocator/RSS/peak','32-bit aggregate suffix arena and block count']}
    log=(out/'commands.log').open('x')
    def run(command,timeout=900):
        meta['commands'].append(list(map(str,command)));log.write('$ '+' '.join(map(str,command))+'\n');log.flush()
        result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=timeout)
        log.write(result.stdout);log.write(result.stderr);log.flush()
        if result.returncode:save(out/'failure.json',{'command':list(map(str,command)),'rc':result.returncode,'stderr':result.stderr[-12000:]});raise RuntimeError('failed command; see commands.log')
        return result
    meta['compiler']=run(['g++','--version']).stdout.splitlines()[0];meta['python']=run(['python3','--version']).stdout.strip()
    cpp=ROOT/'adapters/our_static/our_static_adapter.cpp';header=cpp.with_suffix('.h')
    core_sha=digest(UPSTREAM/'static_physical_index.hpp');adapter_sha=hashlib.sha256((digest(cpp)+digest(header)).encode()).hexdigest()
    common=['-std=c++17','-march=x86-64-v3','-I'+str(UPSTREAM),'-Ilearned-index/src','-Iadapters/our_static',
        '-DSTATIC_CORE_SHA256="'+core_sha+'"','-DSTATIC_ADAPTER_SHA256="'+adapter_sha+'"']
    meta['descriptor']={'core_sha256':core_sha,'adapter_digest':adapter_sha}
    cases=[]
    for corpus in ['tranco','mixed','skew']:
        path=ROOT/('results/matrix2_'+corpus)/'interchange/rows.bin';rows=read_rows(path)
        sets=[('full',path,rows)]
        if corpus=='tranco':
            selected=sorted(random.Random(2026101203).sample(rows,65536));small=out/'tranco_64k_rows.bin';write_rows(small,selected);sets.append(('64k',small,selected))
        for scale,row_path,data in sets:
            for count in [10000,100000]:
                cell=f'{corpus}_{scale}_{count}';q=out/(cell+'_queries.bin');qm=queries(q,data,count)
                cases.append((cell,row_path,q,20 if count==10000 else 2))
                meta['inputs'][cell]={'rows_path':str(row_path.relative_to(WORKSPACE)),'rows_sha256':digest(row_path),'rows':len(data),'queries_path':str(q.relative_to(WORKSPACE)),'queries':qm}
    save(out/'identity.json',meta)
    print('preparing shared interface and full correctness gates',flush=True)
    acceptance=build/'acceptance';check=build/'check';san=build/'san_check'
    defs=['-DACCEPTANCE_FACTORY_HEADER="our_static_adapter.h"','-DACCEPTANCE_FACTORY_SYMBOL=make_our_static_index','-DACCEPTANCE_SAMPLE_PARAMETER=lookup']
    if args.reuse_gates:
        reused=args.reuse_gates.resolve();prior=json.loads((reused/'identity.json').read_text())
        exemptions={'07_Eval_Integration/diagnostics/run_static_interface.py','07_Eval_Integration/diagnostics/bench_static_interface.cpp'}
        for p,sha in prior['sources'].items():
            if p not in exemptions:assert digest(WORKSPACE/p)==sha,('gate source changed',p)
        assert prior['compiler']==meta['compiler'] and prior['descriptor']==meta['descriptor']
        for cell,item in meta['inputs'].items():
            assert item['rows_sha256']==prior['inputs'][cell]['rows_sha256']
            assert item['queries']['sha256']==prior['inputs'][cell]['queries']['sha256']
        gates=json.loads((reused/'gates.json').read_text());assert len(gates)==4
        for gate in gates:
            assert gate['sanitizer_rc']==0 and gate['sanitizer_stderr_bytes']==0
            for field in ['checks','domain_checks','scans','rows','queries','core_bytes']:assert gate[field]==gate['sanitized_work'][field]
        meta['gate_reuse']={'from':str(reused.relative_to(WORKSPACE)),'artifacts':{},'binaries':{}}
        for name in ['gates.json','acceptance_static.txt','acceptance_ours.txt','acceptance_ART.txt','acceptance_HOT.txt']:
            path=reused/name;meta['gate_reuse']['artifacts'][name]=digest(path);shutil.copyfile(path,out/name)
        for path in [acceptance,check,san]:
            source=reused/'build'/path.name;assert digest(source)==prior['binaries'][path.name]
            meta['gate_reuse']['binaries'][path.name]=digest(source);shutil.copyfile(source,path)
        print('unchanged source/input/compiler gates reused; benchmark driver will run its full row/query oracle anew',flush=True)
    else:
        run(['g++','-O2']+common+defs+['adapters/common/contract_acceptance.cpp',str(cpp),'-o',str(acceptance)])
        acceptance_result=run([str(acceptance)]);assert not acceptance_result.stderr
        (out/'acceptance_static.txt').write_text(acceptance_result.stdout)
        check_sources=['diagnostics/check_static_interface.cpp','learned-index/src/unified/runner.cpp',str(cpp)]
        run(['g++','-O2']+common+check_sources+['-o',str(check)])
        run(['g++','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']+common+check_sources+['-o',str(san)])
        gates=[]
        for cell,rows_path,q,_ in cases:
            if not cell.endswith('_100000'):continue
            r=run([str(check),str(rows_path),str(q)]);assert not r.stderr
            s=run([str(san),str(rows_path),str(q)]);assert not s.stderr
            gate=json.loads(r.stdout);san_gate=json.loads(s.stdout)
            # Semantic results and coverage must match, work counts across O2 vs O1+san need not.
            for field in ['checks','domain_checks','scans','rows','queries','core_bytes']:
                assert gate[field]==san_gate[field],('semantic coverage differs',field)
            gate.update(cell=cell,sanitizer_rc=0,sanitizer_stderr_bytes=0,sanitized_work=san_gate);gates.append(gate);save(out/'gates.json',gates)
            print('gate',cell,'checks',gate['checks'],'scans',gate['scans'],'blocks',gate['learned_blocks'],'->',gate['hybrid_blocks'],flush=True)
    # Changed shared gate must retain acceptance for existing dynamic implementations.
    for name,factory,objects,include in [
        ('ours','make_our_index_index',['ours.o'],['-Iadapters/our_index']),
        ('ART','make_art_index',['art.o','art_adapter.o'],['-Iadapters/art']),
        ('HOT','make_hot_index',['hot_adapter.o'],['-Iadapters/hot'])]:
        if args.reuse_gates:continue
        header_name={'ours':'our_index_adapter.h','ART':'art_adapter.h','HOT':'hot_adapter.h'}[name]
        exe=build/('acceptance_'+name)
        objpaths=[str(previous/'build'/p) for p in objects]
        for path in objpaths:meta['reused_objects'][str(Path(path).relative_to(WORKSPACE))]=digest(Path(path))
        run(['g++','-O2']+common+include+['-DACCEPTANCE_FACTORY_HEADER="'+header_name+'"','-DACCEPTANCE_FACTORY_SYMBOL='+factory,'adapters/common/contract_acceptance.cpp']+objpaths+['-o',str(exe)])
        result=run([str(exe)]);assert not result.stderr;(out/('acceptance_'+name+'.txt')).write_text(result.stdout)
    print('correctness passed; compiling the clean six-arm comparison',flush=True)
    objs=[]
    for name in ['art.o','art_adapter.o','hot_adapter.o']:
        path=previous/'build'/name;meta['reused_objects'][str(path.relative_to(WORKSPACE))]=digest(path);objs.append(str(path))
    bench=build/'bench'
    run(['g++','-O3']+common+['diagnostics/bench_static_interface.cpp','learned-index/src/unified/runner.cpp',str(cpp)]+objs+['-o',str(bench)])
    meta['binaries']={p.name:digest(p) for p in [acceptance,check,san,bench]};save(out/'identity.json',meta)
    raw=[]
    for cell,rows_path,q,passes in cases:
        for process in range(5):
            r=run(['taskset','-c',str(cpu),str(bench),str(rows_path),str(q),str(process),str(passes)]);assert not r.stderr
            records=[json.loads(line) for line in r.stdout.splitlines()];assert len(records)==42;verify_records(records)
            for row in records:row.update(cell=cell,process=process)
            raw.extend(records);save(out/'raw.json',raw);print('timing',cell,'process',process+1,'done',flush=True)
        save(out/'summary.json',summarize(raw))
    summary=summarize(raw);skew=[c for c in summary if c['cell'].startswith('skew_')]
    risks=[c for c in summary if not c['cell'].startswith('skew_')]
    decision={'interface_accepted':True,'integration_regression':any(c['integration_regression'] for c in summary),
        'skew_gain_pass':all(c['candidate_gain'] for c in skew),'risk_regression':any(c['candidate_regression'] for c in risks),
        'production_default_changed':False,'readonly_default':'learned','dynamic_support':False}
    decision['retain_wide_directory']=not decision['integration_regression'] and decision['skew_gain_pass'] and not decision['risk_regression']
    for p,sha in identity.items():assert digest(WORKSPACE/p)==sha,('source drift',p)
    save(out/'decision.json',decision);save(out/'identity.json',meta);log.close()
    log_bytes=(out/'commands.log').read_bytes();archive=out/'commands.log.gz'
    with archive.open('xb') as f:
        with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as gz:gz.write(log_bytes)
    assert gzip.decompress(archive.read_bytes())==log_bytes
    save(out/'log_archive.json',{'original_sha256':hashlib.sha256(log_bytes).hexdigest(),'archive_sha256':digest(archive),'original_bytes':len(log_bytes)})
    (out/'commands.log').unlink()
    print('completed',json.dumps(decision),flush=True)

if __name__=='__main__':main()

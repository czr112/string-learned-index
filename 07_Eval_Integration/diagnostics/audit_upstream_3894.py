"""Isolated upstream refresh: preserve old performance identity, test retained new factories."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import argparse

ROOT=Path(__file__).resolve().parents[1]
WS=ROOT.parent
OLD=ROOT/'upstream_snapshots/model_e8e05411'
NEW=ROOT/'upstream_snapshots/model_3894c767'
BENCH=ROOT/'upstream_snapshots/benchmark_003a55bf'
ART_SOURCE=WS/'02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart'
PRIOR=ROOT/'results/bm_cost_native_20261019b'
OLD_ID='e8e05411135b222edb82e3861799a2ae07ae5470'
NEW_ID='3894c767338a865d2920ba5e21283827fea1d803'
CORE=['block_model.cpp','block_model.h','key_bytes.h','query_cost.cpp','query_cost.h',
      'region_planner.cpp','region_planner.h','learned_index.cpp','learned_index.h',
      'residual_tags.cpp','residual_tags.h','short_directory.h','tag_probe.h','benchmark/calibrate_planner.cpp']


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',default='upstream_update_audit_20261019')
    args=parser.parse_args();assert Path(args.output).name==args.output
    prior=read(PRIOR/'identity.json');gates=read(PRIOR/'gates.json')
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=NEW,text=True).strip()==NEW_ID
    out=ROOT/'results'/args.output;out.mkdir(exist_ok=False)
    cache=(WS/'builds/native/CMakeCache.txt').read_text()
    cmake=Path(re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$',cache,re.M).group(1))
    ctest=cmake.with_name('ctest');assert cmake.is_file() and ctest.is_file()
    meta={'model_before':OLD_ID,'model_after':NEW_ID,'benchmark':prior['benchmark_commit'],
          'sources':{},'artifacts':{},'inputs':prior['inputs'],'commands':[],
          'cost_reference_identity_sha256':sha(PRIOR/'identity.json'),
          'build_tools':{'cmake':str(cmake),'cmake_sha256':sha(cmake),'ctest':str(ctest),'ctest_sha256':sha(ctest)},
          'profile_sha256':sha(PRIOR/'planner-costs.txt'),'cpu':min(os.sched_getaffinity(0)),
          'same_core_files':{p:{'old':sha(OLD/'src'/p),'new':sha(NEW/'src'/p)} for p in CORE},
          'native_art_source':str(ART_SOURCE.relative_to(WS)),
          'performance_rerun':False,'limits':['Release CTest and corpus compatibility only',
              'native ART uses the existing pinned local libart source; HOT unavailable; no new baseline ranking',
              'old timing remains bound to old revision; byte equality is not a new timing measurement']}
    assert all(v['old']==v['new'] for v in meta['same_core_files'].values())
    files=subprocess.check_output(['git','ls-files'],cwd=NEW,text=True).splitlines()
    paths=[NEW/p for p in files if (NEW/p).is_file()]
    paths += [Path(__file__),ROOT/'diagnostics/check_bm_cost.cpp',ROOT/'diagnostics/borrowed_contract_acceptance.cpp',
              ROOT/'diagnostics/generate_borrowed_contract_acceptance.py',BENCH/'src/index_contract.h',
              BENCH/'src/unified/runner.cpp',BENCH/'src/unified/runner.h',ART_SOURCE/'src/art.c',ART_SOURCE/'src/art.h']
    for p in sorted(set(paths)):
        rel=str(p.relative_to(WS));meta['sources'][rel]=sha(p)
        dest=out/'frozen_sources'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(p.read_bytes())
    diff=subprocess.check_output(['git','diff','--binary',OLD_ID,NEW_ID],cwd=NEW)
    (out/'upstream.patch.gz').write_bytes(gzip.compress(diff,mtime=0))
    meta['diff_sha256']=hashlib.sha256(diff).hexdigest()
    save(out/'identity.json',meta)
    log=(out/'commands.log').open('x',encoding='utf-8')

    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command)
        log.write('$ '+' '.join(command)+'\n');log.flush()
        p=subprocess.run(command,cwd=WS,capture_output=True,env=env,timeout=3600)
        stdout=p.stdout.decode('utf-8',errors='backslashreplace');stderr=p.stderr.decode('utf-8',errors='backslashreplace')
        log.write(stdout);log.write(stderr);log.flush()
        if p.returncode:
            save(out/'failure.json',{'command':command,'rc':p.returncode,'stdout':stdout[-10000:],'stderr':stderr[-10000:]})
            save(out/'identity.json',meta);raise RuntimeError('upstream compatibility failure retained')
        return stdout

    print('configure isolated 3894 upstream',flush=True)
    build=out/'build'
    run([cmake,'-S',NEW,'-B',build,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_FLAGS=-march=x86-64-v3',
         '-DSTRING_INDEX_CONTRACT_INCLUDE_DIR='+str(BENCH/'src'),'-DBUILD_TESTING=ON',
         '-DART_SOURCE_DIR='+str(ART_SOURCE)])
    print('build upstream tests',flush=True)
    run([cmake,'--build',build,'-j','2'])
    print('run complete upstream CTest (no performance ranking)',flush=True)
    result=run([ctest,'--test-dir',build,'--output-on-failure'])
    m=re.search(r'100% tests passed, 0 tests failed out of (\d+)',result);assert m,result
    tests=int(m.group(1));assert tests>=10,tests
    exe=build/'gate_latest'
    run(['g++','-pthread','-std=c++17','-O3','-DNDEBUG','-march=x86-64-v3',
         '-I'+str(BENCH/'src'),'-I'+str(NEW/'src'),'-I'+str(ROOT/'diagnostics'),
         ROOT/'diagnostics/check_bm_cost.cpp',BENCH/'src/unified/runner.cpp',
         build/'src/libblocklearn_adapter.a',build/'src/libblocklearn_core.a','-o',exe])
    records=[]
    env=dict(os.environ,BLOCKLEARN_COST_PROFILE=str(PRIOR/'planner-costs.txt'))
    for previous in gates:
        cell=previous['cell'];item=prior['inputs'][cell]
        assert sha(WS/item['rows_path'])==item['rows_sha256'] and sha(WS/item['queries_path'])==item['queries_sha256']
        output=run(['taskset','-c',meta['cpu'],exe,WS/item['rows_path'],WS/item['queries_path']],env)
        record=json.loads(output.splitlines()[-1]);record['cell']=cell
        assert record==previous,('new snapshot retained-factory work/answer drift',cell)
        records.append(record);save(out/'gates_latest.json',records)
        print('new retained factories match old counters',cell,flush=True)
    for path,digest in meta['sources'].items():assert sha(WS/path)==digest,('source drift',path)
    for p in [exe,build/'src/libblocklearn_adapter.a',build/'src/libblocklearn_core.a',build/'Testing/Temporary/LastTest.log']:
        meta['artifacts'][str(p.relative_to(out))]=sha(p)
    assert sha(PRIOR/'planner-costs.txt')==meta['profile_sha256']
    save(out/'identity.json',meta)
    log.close();data=(out/'commands.log').read_bytes()
    (out/'commands.log.gz').write_bytes(gzip.compress(data,mtime=0))
    meta_checks={'status':'verified','ctest_passed':tests,'core_files_byte_identical':len(CORE),
        'normal_corpus_cells':len(records),'same_retained_factory_counters':True,'performance_rerun':False,
        'model_after':NEW_ID,'source_files':len(meta['sources']),
        'raw_log_sha256':hashlib.sha256(data).hexdigest(),'gzip_log_sha256':sha(out/'commands.log.gz')}
    save(out/'verification.json',meta_checks);(out/'commands.log').unlink()
    print(json.dumps(meta_checks),flush=True)


if __name__=='__main__':main()

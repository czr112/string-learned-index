"""Independent real-key generalization of the already frozen BM implementation."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys

from run_bm_latest_read import summarize, STRATEGIES, ARMS

WORKSPACE=Path(__file__).resolve().parents[2]
ROOT=WORKSPACE/'07_Eval_Integration'
BENCHMARK=ROOT/'upstream_snapshots/benchmark_003a55bf'
NAMES=['english_words','memetracker_urls','memetracker_phrases']
COUNT=65536

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda:f.read(1024*1024),b''):h.update(part)
    return h.hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',default='bm_real_20261018');a=p.parse_args()
    assert Path(a.output).name==a.output
    previous=ROOT/'results/bm_latest_read_20261018';semantic=ROOT/'results/bm_latest_page_20261018'
    assert read(previous/'independent_verification.json')['status']=='verified'
    assert not read(previous/'decision.json')['BM_optimization_gate_passed']
    assert read(semantic/'independent_verification.json')['original_BM_used']
    before=read(previous/'identity.json');entry=read(semantic/'identity.json');sources=dict(before['sources'])
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest,('source drift',path)
    for path in [Path(__file__),ROOT/'diagnostics/verify_bm_real.py',ROOT/'diagnostics/BM_REAL_SPEC_20261018.md',BENCHMARK/'datasets.lock.json',
                 ROOT/'diagnostics/fetch_bm_real_sources.py',ROOT/'diagnostics/publish_bm_real_source.py',
                 ROOT/'results/bm_real_preparation_20261018/attempt1.json',
                 BENCHMARK/'data/raw/memetracker-native-fetch.json',BENCHMARK/'data/raw/memetracker-native-publication.json',
                 *[BENCHMARK/'scripts'/n for n in ('prepare_real_datasets.py','make_dataset.py','make_lookup_workload.py')],
                 *list((BENCHMARK/'scripts/benchmark_lib').glob('*.py'))]:
        sources[str(path.relative_to(WORKSPACE))]=sha(path)
    out=ROOT/'results'/a.output;out.mkdir(exist_ok=False);(out/'interchange').mkdir()
    reused={}
    for directory,names in [(semantic,('bm_check_normal','bm_check_sanitized')),
                            (previous,('bm_read_normal','bm_read_sanitized'))]:
        identity=read(directory/'identity.json')
        for name in names:
            path=directory/'build'/name;digest=sha(path)
            assert digest==identity['artifacts'][str(path.relative_to(directory))]
            reused[str(path.relative_to(WORKSPACE))]=digest
    meta={'sources':sources,'inputs':{},'raw_sources':{},'reused_artifacts':reused,'commands':[],
          'created_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'upstream_commit':before['upstream_commit'],'benchmark_commit':before['benchmark_commit'],
          'previous_identity_path':str((previous/'identity.json').relative_to(WORKSPACE)),
          'previous_identity_sha256':sha(previous/'identity.json'),
          'semantic_identity_path':str((semantic/'identity.json').relative_to(WORKSPACE)),
          'semantic_identity_sha256':sha(semantic/'identity.json'),
          'strategies':STRATEGIES,'arms':ARMS,'processes':5,'repeats':10,'cpu':before['cpu'],
          'fit':entry['fit'],'sampling':{'count':COUNT,'seed':42,'query_seed':20261018},
          'preparation_notes':'initial 300-second download failed after English completion; identical locked source fetched natively; hardlink publication denied, then SHA-verified same-directory rename; unchanged upstream sampler run offline for the two MemeTracker fields',
          'preparation_attempt_path':'07_Eval_Integration/results/bm_real_preparation_20261018/attempt1.json',
          'excluded_baseline':before['excluded_baseline'],
          'limits':['real keys with synthetic queries','static reads after existing value updates',
                    'self bytes exclude node/allocator metadata and scratch','old optimization rejection remains in force']}
    def save(name,obj):(out/name).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
    def archive(path):
        target=out/'frozen_sources'/path;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(WORKSPACE/path,target)
    for path in sources:archive(path)
    save('identity.json',meta)
    log=(out/'commands.log').open('x',encoding='utf-8')
    def run(command,env=None):
        command=list(map(str,command));meta['commands'].append(command);save('identity.json',meta)
        log.write('$ '+' '.join(command)+'\n');log.flush()
        r=subprocess.run(command,cwd=WORKSPACE,capture_output=True,text=True,env=env,timeout=1200)
        log.write(r.stdout+r.stderr);log.flush()
        if r.returncode or r.stderr:
            save('failure.json',{'command':command,'rc':r.returncode,'stderr':r.stderr});raise RuntimeError('real-data gate failed')
        return [json.loads(s) for s in r.stdout.splitlines() if s.startswith('{')]
    sys.path.insert(0,str(BENCHMARK/'scripts'))
    from benchmark_lib.readers import iter_keys
    from benchmark_lib.interchange import write_rows,write_queries
    catalog=read(BENCHMARK/'datasets.lock.json')
    publication=read(BENCHMARK/'data/raw/memetracker-native-publication.json')
    assert publication['status']=='verified' and publication['sha256']==catalog['sources']['memetracker-clusters']['sha256']
    for source in catalog['sources'].values():
        path=BENCHMARK/'data/raw'/source['filename']
        assert path.stat().st_size==source['bytes'] and sha(path)==source['sha256']
        meta['raw_sources'][str(path.relative_to(WORKSPACE))]={'sha256':source['sha256'],'bytes':source['bytes']}
    for name in NAMES:
        path=BENCHMARK/'data/real'/f'{name}_{COUNT}_s42_string';sidecar=Path(str(path)+'.json');info=read(sidecar)
        assert info['dataset']==name and info['keys']==info['requested_keys']==COUNT and info['seed']==42
        assert sha(path)==info['dataset_sha256'] and info['generator_sha256']==sha(BENCHMARK/'scripts/prepare_real_datasets.py')
        assert info['source']==catalog['sources'][catalog['datasets'][name]['source']]
        assert info['scan']['scan']=='complete source file'
        keys=list(iter_keys(path,'legacy-tli'));assert len(keys)==COUNT and keys==sorted(set(keys))
        rows=out/'interchange'/(name+'_rows.bin');queries=out/'interchange'/(name+'_queries.bin')
        row_hash=write_rows(rows,keys)
        items=[(key,True,i) for i,key in enumerate(keys)];random.Random(20261018).shuffle(items)
        query_hash=write_queries(queries,items)
        sidecar_key=str(sidecar.relative_to(WORKSPACE));sources[sidecar_key]=sha(sidecar);archive(sidecar_key)
        meta['inputs'][name]={'rows_path':str(rows.relative_to(WORKSPACE)),'rows_sha256':row_hash,'rows':COUNT,
                             'queries_path':str(queries.relative_to(WORKSPACE)),
                             'queries':{'count':COUNT,'sha256':query_hash,'kind':'one per source key; driver replaces every tenth by a proved-absent suffix key'},
                             'dataset_path':str(path.relative_to(WORKSPACE)),'dataset_sha256':sha(path),
                             'sidecar_path':sidecar_key,'sidecar_sha256':sha(sidecar),
                             'dataset_provenance':info}
        print('frozen dataset',name,info['statistics'],flush=True)
    save('identity.json',meta)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    corpus={}
    for name,item in meta['inputs'].items():
        corpus[name]={}
        for kind in ('normal','sanitized'):
            records=run([semantic/'build'/('bm_check_'+kind),WORKSPACE/item['rows_path']],env)
            assert records[-1]['status']=='passed';corpus[name][kind]=records;save('corpus_gates.json',corpus)
            print('corpus gate',name,kind,records[-1]['checks'],flush=True)
        assert corpus[name]['normal']==corpus[name]['sanitized']
    def command(kind,item,state,process,mode):
        return ['taskset','-c',meta['cpu'],previous/'build'/('bm_read_'+kind),WORKSPACE/item['rows_path'],WORKSPACE/item['queries_path'],state,process,mode]
    gates=[]
    for name,item in meta['inputs'].items():
        for state in ('clean','dirty'):
            records={kind:run(command(kind,item,state,0,'gate'),env)[0] for kind in ('normal','sanitized')}
            assert records['normal']==records['sanitized']
            g=records['normal'];assert g['root_publications']==1 and g['BM_calls']>0 and g['BM_calls']+g['prefix_rejections']==COUNT
            gates.append({'cell':name+'/'+state,**records});save('gates.json',gates)
            print('full gate',name,state,g['checks'],flush=True)
    raw=[]
    for name,item in meta['inputs'].items():
        for state in ('clean','dirty'):
            cell=name+'/'+state;expected=next(g['normal'] for g in gates if g['cell']==cell)
            for process in range(5):
                records=run(command('normal',item,state,process,'timing'))
                assert len(records)==101 and records[0]==expected
                for row in records[1:]:
                    assert row['operations']==COUNT and row['elapsed_ns']>0
                    raw.append({'cell':cell,'process':process,**row})
                save('raw.json',raw)
            print('timing',cell,'x5 complete',flush=True)
    assert len(raw)==3000
    result=summarize(raw);save('summary.json',result)
    save('decision.json',{'characterization_completed':True,'previous_optimization_gate_passed':False,
                          'production_adopted':False,'dynamic_throughput_measured':False,
                          'BM_ART_numerical_target_met_all':all(r['BM_ART_numerical_target_met'] for r in result),
                          'BM_ART_target_failures':[r['cell'] for r in result if not r['BM_ART_numerical_target_met']]})
    for path,digest in sources.items():assert sha(WORKSPACE/path)==digest
    for path,digest in reused.items():assert sha(WORKSPACE/path)==digest
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256'] and sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
        assert sha(WORKSPACE/item['dataset_path'])==item['dataset_sha256']
    save('verification.json',{'status':'passed','corpus_checks_per_build':sum(c['normal'][-1]['checks'] for c in corpus.values()),
                              'read_gate_checks_per_build':sum(g['normal']['checks'] for g in gates),'timing_rows':len(raw)})
    save('identity.json',meta);log.close()
    for row in result:print(row['cell'],{k:round(row['ratios'][k]['median'],3) for k in ('BM/BM_base','BM/F5','BM/ART')},flush=True)

if __name__=='__main__':main()

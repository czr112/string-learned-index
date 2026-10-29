"""Exact historical grouping and query sets, with the transaction factory as candidate.

Reuses prepare_case and analyze_blocks from the frozen query recheck implementation.
16 process blocks, same 40ms calibration, same group/AA/CI rules. No old samples are
pooled with this run. --control bm0|bm1 selects a direct control. All historical
groups are validated semantically; only the unchanged primary mixed90 is timed.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import run_bm_query_recheck as query
from run_bm_tx import ROOT,DIAG,Run,sha,save

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--control',choices=('bm0','bm1'),default='bm0')
    args=parser.parse_args();run=Run(args.output)
    run.sources[str(Path(__file__).resolve())]=sha(Path(__file__))
    run.sources[str(Path(query.__file__).resolve())]=sha(query.__file__)
    try:
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        sources=[DIAG/'bm_tx_query.cpp',model]
        definitions={'BM_QUERY_PRIMARY_ONLY':'1'}
        if args.control=='bm1':definitions.update(BM_TX_CONTROL_BM1='1',BM_QUERY_BM1_ADAPTER_SHA256=sha(DIAG/'bm_split_adapter.hpp'))
        opt=run.build('query',sources,definitions=definitions);san=run.build('query_san',sources,True,definitions)
        manifests=[ROOT/'07_Eval_Integration/results'/d/'manifest.json' for d in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025')]
        cases={};case_root=run.output/'cases';case_root.mkdir()
        for manifest in manifests:
            for name,info in json.loads(manifest.read_text()).items():
                rows=ROOT/info['rows_path'];ops=ROOT/info['operations_path'];pool=ROOT/'07_Eval_Integration/results'/query.SOURCES[info['dataset']]
                if sha(rows)!=info['rows_sha256'] or sha(ops)!=info['operations_sha256']:raise RuntimeError('input hash mismatch')
                meta,_=query.prepare_case(name,query.read_rows(rows.read_bytes()),query.read_ops(ops.read_bytes()),query.read_rows(pool.read_bytes()),info['hot_range'],case_root/name)
                meta['paths']=list(map(str,[rows,ops,pool,case_root/name/'final.bin',case_root/name/'queries.bin']))
                cases[name]=meta
        save(run.output/'cases.json',cases)
        save(run.output/'registration.json',{'processes':16,'min_calibration_ms':40,'groups':'same as 20261024 correct-group protocol',
                                           'candidate':'make_bm_tx_experiment_index','control':args.control,
                                           'timed_groups':['mixed90'],'semantic_groups':'all historical groups',
                                           'runner_sha256':sha(Path(__file__)),'analysis_sha256':sha(query.__file__)})
        run.report['results']={};cpu=min(os.sched_getaffinity(0))
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1')
        # Complete semantic checks before starting any timing.
        for name,meta in cases.items():
            run.execute([san,'check',*meta['paths']],name+'_san.log',env)
        for name,meta in cases.items():
            calibrated=json.loads(run.execute(['taskset','-c',cpu,opt,'calibrate',*meta['paths'],40],name+'_calibration.json'))
            loops=','.join(map(str,calibrated['loops']));blocks=[]
            for process in range(16):
                log=run.execute(['taskset','-c',cpu,opt,'bench',*meta['paths'],process,loops],f'{name}_p{process:02d}.json')
                blocks.append(json.loads(log))
            run.report['results'][name]=query.analyze_blocks(blocks,['mixed90'])
            save(run.output/'report.json',run.report)
            primary=run.report['results'][name]['mixed90']['contrasts']['candidate_vs_bm_dynamic']
            print(f"query {name}: {primary['ratio']:.4f} {primary['verdict']}",flush=True)
        run.finish();print('exact query recheck complete',flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

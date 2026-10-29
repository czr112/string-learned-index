"""Deterministic read work diagnosis; never overlaps query timing."""
from pathlib import Path
import argparse
import json
import os
from run_bm_tx import Run,ROOT,DIAG,save,sha

PROTOCOL=ROOT/'docs/PROTOCOL_BM_READ_WORK_20261030.md'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--calibration',type=Path,required=True)
    args=parser.parse_args()
    calibration=json.loads((args.calibration/'report.json').read_text())
    if calibration['status']!='complete' or calibration['phase']!='calibrate':raise ValueError('calibration not complete')
    hashes=json.loads((args.calibration/'input_hashes.json').read_text())
    cases=json.loads((ROOT/'builds/bm_tx_query_bm1_20261030/cases.json').read_text())
    if len(cases)!=32:raise ValueError('cell coverage')
    for meta in cases.values():
        for p in meta['paths']:
            if sha(p)!=hashes[p]:raise ValueError('input drift')
    run=Run(args.output)
    run.sources.update({str(Path(__file__).resolve()):sha(Path(__file__)),str(PROTOCOL):sha(PROTOCOL)})
    try:
        save(run.output/'input_hashes.json',hashes);save(run.output/'cases.json',cases)
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        definitions={'BM_TX_CONTROL_BM1':'1','BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp')}
        run.report['builds']={}
        for name,sanitize in (('opt',False),('sanitizer',True)):
            binary=run.build('read_work_'+name,[DIAG/'bm_read_work.cpp',model],sanitize,definitions)
            results={}
            for cell,meta in sorted(cases.items()):
                raw=run.execute([binary,'profile',*meta['paths']],f'{name}_{cell}.jsonl',
                                dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
                rows=[json.loads(line) for line in raw.splitlines()]
                if len(rows)!=3 or {r['arm'] for r in rows}!={'bm1_dynamic','tx_dynamic','common_snapshot'}:
                    raise ValueError('incomplete profile')
                results[cell]=rows;run.report['builds'][name]=results;save(run.output/'report.json',run.report)
            print(name+': complete 32 cells',flush=True)
        if run.report['builds']['opt']!=run.report['builds']['sanitizer']:raise ValueError('counter instability across builds')
        if any(sha(p)!=h for p,h in hashes.items()):raise ValueError('input drift at completion')
        run.finish();print('Read work diagnosis complete',flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

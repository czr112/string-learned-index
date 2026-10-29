"""Check allocation-free public-factory reads after all 32 frozen mutation traces."""
import argparse
import json
import os
from pathlib import Path
from run_bm_tx import Run, ROOT, DIAG, save, sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    cases=json.loads((ROOT/'builds/bm_tx_query_bm1_20261030/cases.json').read_text())
    manifests={}
    for name in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025'):
        manifests.update(json.loads((ROOT/'07_Eval_Integration/results'/name/'manifest.json').read_text()))
    if len(manifests)!=32 or set(cases)!=set(manifests):raise ValueError('cell coverage')
    run=Run(args.output)
    try:
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        run.report['builds']={}
        for name,sanitize in (('opt',False),('sanitizer',True)):
            binary=run.build('reads_'+name,[DIAG/'check_bm_tx_reads.cpp',model],sanitize)
            results={}
            for cell,meta in sorted(manifests.items()):
                rows=ROOT/meta['rows_path'];ops=ROOT/meta['operations_path']
                query=ROOT/'builds/bm_tx_query_bm1_20261030/cases'/cell/'queries.bin'
                if (sha(rows)!=meta['rows_sha256'] or sha(ops)!=meta['operations_sha256']
                        or sha(query)!=cases[cell]['query_sha256']):raise ValueError('input drift')
                result=run.execute([binary,rows,ops,query],f'{name}_{cell}.jsonl',
                                   dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
                results[cell]=dict(json.loads(result),rows_sha256=sha(rows),operations_sha256=sha(ops),query_sha256=sha(query))
            run.report['builds'][name]=results;save(run.output/'report.json',run.report)
        run.finish()
        print(json.dumps({'status':'complete','builds':{k:{'cells':len(v),'queries':sum(x['queries'] for x in v.values()),
                         'allocations':sum(x['allocations'] for x in v.values())} for k,v in run.report['builds'].items()}}))
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise


if __name__=='__main__':main()

"""Registered read-only Model16 factory gates; no timers or adoption decision."""
from pathlib import Path
import argparse
import json
import os
from run_bm_tx import Run,ROOT,DIAG,save,sha
from run_bm_model16_work import generate

PROTOCOL=ROOT/'docs/PROTOCOL_BM_MODEL16_SNAPSHOT_20261030.md'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--nomination',type=Path,required=True)
    parser.add_argument('--compile-only',action='store_true')
    args=parser.parse_args()
    evidence=args.nomination.resolve()
    frozen_evidence=ROOT/'07_Eval_Integration/results/bm_model16_work_20261030'
    for filename in ('report.json','generation.json','cases.json','input_hashes.json'):
        if sha(evidence/filename)!=sha(frozen_evidence/filename):
            raise ValueError('nomination differs from archived accepted source: '+filename)
    nominated=json.loads((evidence/'report.json').read_text())
    if nominated['status']!='complete' or not nominated['nomination']['snapshot']['nominate']:
        raise ValueError('snapshot nomination required')
    run=Run(args.output)
    for p in (Path(__file__).resolve(),PROTOCOL,ROOT/'tools/run_bm_model16_work.py'):
        run.sources[str(p)]=sha(p)
    try:
        generated=generate(run)
        # Bind the generator to the exact header bytes already nominated, not a label.
        old=json.loads((evidence/'generation.json').read_text())
        current=json.loads((run.output/'generation.json').read_text())
        if {k:v['generated_sha256'] for k,v in old.items()}!={k:v['generated_sha256'] for k,v in current.items()}:
            raise ValueError('nominated generated source drift')
        definitions={'BM_MODEL16_INDEX_HEADER':str(generated),'BM_TX_CONTROL_BM1':'1',
                     'BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp'),
                     'BM_MODEL16_ADAPTER_SHA256':sha(DIAG/'bm_model16_snapshot_adapter.hpp')}
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        cases=json.loads((evidence/'cases.json').read_text());inputs=json.loads((evidence/'input_hashes.json').read_text())
        if len(cases)!=32 or any(sha(p)!=h for p,h in inputs.items()):raise ValueError('snapshot input drift')
        save(run.output/'cases.json',cases);save(run.output/'input_hashes.json',inputs)
        save(run.output/'registration.json',{'scope':'snapshot only','protocol_sha256':sha(PROTOCOL),
             'nomination_report_sha256':sha(evidence/'report.json'),'terminal_limit':1.05,'build_peak_limit':1.05,
             'timing_claims':False,'fault_continuation_reads':1000})
        contract=run.output/'contract.cpp'
        contract.write_bytes(b'#define ACCEPTANCE_FACTORY_HEADER "bm_model16_snapshot_adapter.hpp"\n'
                             b'#define ACCEPTANCE_FACTORY_SYMBOL make_bm_model16_snapshot_index\n'
                             b'#include "borrowed_contract_acceptance.cpp"\n')
        corpus=ROOT/'07_Eval_Integration/results/bm_real_20261018/interchange'
        fixtures=[('bytes_'+str(n),['--bytes',str(n)]) for n in (0,15,16,17,2048,2049)]
        fixtures += [(name,[str(corpus/(name+'_rows.bin')),'2049']) for name in
                     ('english_words','memetracker_urls','memetracker_phrases')]
        corpus_hashes={args[0]:sha(args[0]) for _,args in fixtures if args[0]!='--bytes'}
        save(run.output/'fault_input_hashes.json',corpus_hashes)
        run.report['builds']={}
        for name,sanitize in (('opt',False),('sanitizer',True)):
            binary=run.build('snapshot_'+name,[DIAG/'check_bm_model16_snapshot.cpp',model],sanitize,definitions)
            contract_binary=run.build('contract_'+name,[contract,model],sanitize,definitions)
            if args.compile_only:continue
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1')
            result={'contract':run.execute([contract_binary],name+'_contract.log',env),'cells':{},'fault':{}}
            run.report['builds'][name]=result
            for cell,meta in sorted(cases.items()):
                records=[json.loads(x) for x in run.execute([binary,'profile',*meta['paths']],name+'_'+cell+'.jsonl',env).splitlines()]
                if len(records)!=2 or [r['arm'] for r in records]!=['tx8_snapshot','m16_snapshot']:
                    raise ValueError('snapshot profile coverage')
                base,candidate=records
                terminal=candidate['terminal']/base['terminal'];peak=candidate['build_peak']/base['build_peak']
                if terminal>1.05 or peak>1.05 or any(r['find_allocations'] or r['mismatches'] for r in records):
                    raise ValueError('snapshot space/correctness gate')
                result['cells'][cell]={'records':records,'terminal_ratio':terminal,'build_peak_ratio':peak}
                save(run.output/'report.json',run.report)
            print(name+': snapshot32 complete',flush=True)
            for fixture,paths in fixtures:
                raw=run.execute([binary,'fault',*paths],name+'_fault_'+fixture+'.jsonl',env)
                records=[json.loads(x) for x in raw.splitlines()]
                summary=records[-1]
                if summary['suite']!='fault_summary' or summary['mismatches']:raise ValueError('fault summary')
                events=[r for r in records if 'ordinal' in r]
                for path in ('bulk_load','scan'):
                    selected=[r for r in events if r['path']==path]
                    count=selected[0]['allocation_count']
                    if [r['ordinal'] for r in selected]!=list(range(1,count+2)) or sum(r['injected'] for r in selected)!=count:
                        raise ValueError('missing fault ordinal')
                result['fault'][fixture]={'summary':summary,'injected':sum(r['injected'] for r in events),
                    'positive_controls':sum(not r['injected'] for r in events),'records':len(events)}
                save(run.output/'report.json',run.report);print(name+': fault '+fixture+' complete',flush=True)
        if not args.compile_only:
            a,b=run.report['builds']['opt'],run.report['builds']['sanitizer']
            if a!=b:raise ValueError('snapshot deterministic build disagreement')
            run.report['functional_space_acceptance']=True
        if any(sha(p)!=h for p,h in {**inputs,**corpus_hashes}.items()):raise ValueError('snapshot completion input drift')
        run.finish();print(json.dumps({'status':'complete','compile_only':args.compile_only}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

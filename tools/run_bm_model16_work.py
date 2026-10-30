"""One fixed BlockModel segment-budget intervention; work/space, never latency."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
from run_bm_tx import Run,ROOT,DIAG,save,sha

PROTOCOL=ROOT/'docs/PROTOCOL_BM_MODEL16_WORK_20261030.md'
BASE='10e0494'

def generate(run):
    directory=run.output/'model16_sources';directory.mkdir()
    names={'bm_n1_page_core.hpp':'bm_m16_page_core.hpp','bm_n1_page_root.hpp':'bm_m16_page_root.hpp',
           'bm_n1_index.hpp':'bm_m16_index.hpp','bm_tx_index.hpp':'bm_m16_tx_index.hpp'}
    records={}
    for original,new in names.items():
        source=DIAG/original;committed=subprocess.check_output(['git','-C',str(ROOT),'show',BASE+':'+source.relative_to(ROOT).as_posix()])
        if source.read_bytes()!=committed:raise ValueError('model control preimage drift')
        text=committed.decode()
        for before,after in names.items():text=text.replace(before,after)
        for before,after in (('bm_n1_leaf','bm_m16_leaf'),('bm_n1_page','bm_m16_page'),('bm_tx_page','bm_m16_tx')):
            text=text.replace(before,after)
        if original=='bm_n1_page_core.hpp':
            for variable in ('training','train'):
                training=f'BlockModel::fit({variable},1,7,8,1)'
                if text.count(training)!=1:raise ValueError('unexpected model training site: '+variable)
                text=text.replace(training,f'BlockModel::fit({variable},1,7,16,1)',1)
            if text.count('BlockModel-fence-1-7-8-1')!=1:raise ValueError('unexpected model identity')
            text=text.replace('BlockModel-fence-1-7-8-1','BlockModel-fence-1-7-16-1',1)
        target=directory/new;target.write_bytes(text.encode())
        records[original]={'revision':BASE,'preimage_sha256':hashlib.sha256(committed).hexdigest(),
                           'generated':str(target),'generated_sha256':sha(target)}
    save(run.output/'generation.json',records)
    return directory/'bm_m16_tx_index.hpp'

def nominate(cells):
    scopes={}
    for scope in ('dynamic','snapshot'):
        records={};all_guards=True;reduced=0
        for cell,rows in cells.items():
            arms={r['arm']:r for r in rows};base=arms['tx8_'+scope];candidate=arms['m16_'+scope]
            groups={r['group']:r for r in base['groups']};other={r['group']:r for r in candidate['groups']}
            b=groups['mixed90'];c=other['mixed90']
            if not b['queries'] or b['queries']!=c['queries']:raise ValueError('model query coverage')
            terminal=candidate['terminal']/base['terminal'];peak=candidate['maintenance_peak']/base['maintenance_peak']
            blocks=c['payload_blocks']/b['payload_blocks'];exact=c['exact_compares']/b['exact_compares']
            added=(c['directory_steps']-b['directory_steps'])/b['queries']
            guard=(terminal<=1.05 and peak<=1.10 and exact<=1.05 and added<=0.5
                   and candidate['find_allocations']==0 and candidate['shape_and_payload_signatures_match'])
            all_guards=all_guards and guard;reduced+=blocks<=0.75
            records[cell]={'terminal_ratio':terminal,'maintenance_peak_ratio':peak,'payload_blocks_ratio':blocks,
                           'exact_compare_ratio':exact,'added_directory_steps_per_query':added,'guard_pass':guard}
        scopes[scope]={'cells':records,'all_guards_pass':all_guards,'reduced_cells':reduced,
                       'nominate':all_guards and reduced>=24}
    return scopes

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--qualification',type=Path,required=True)
    args=parser.parse_args()
    qualification=json.loads((args.qualification/'report.json').read_text())
    if qualification['status']!='complete' or qualification['phase']!='calibrate':raise ValueError('qualification must finish first')
    run=Run(args.output)
    run.sources.update({str(Path(__file__).resolve()):sha(Path(__file__)),str(PROTOCOL):sha(PROTOCOL)})
    try:
        generated=generate(run)
        definitions={'BM_MODEL16_INDEX_HEADER':str(generated),'BM_TX_CONTROL_BM1':'1',
                     'BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp')}
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        cases=json.loads((ROOT/'builds/bm_tx_query_bm1_20261030/cases.json').read_text());manifests={}
        for name in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025'):
            manifests.update(json.loads((ROOT/'07_Eval_Integration/results'/name/'manifest.json').read_text()))
        historical=json.loads((ROOT/'07_Eval_Integration/results/bm_query_recheck_20261024/cases.json').read_text())
        if len(cases)!=32 or set(cases)!=set(manifests):raise ValueError('model cell coverage')
        inputs={}
        for cell,meta in cases.items():
            old=historical[cell];entry=manifests[cell]
            expected=[entry['rows_sha256'],entry['operations_sha256'],old['input_sha256']['pool'],old['final_sha256'],old['query_sha256']]
            if len(meta['paths'])!=5:raise ValueError('model input arity')
            for p,h in zip(meta['paths'],expected):
                if sha(p)!=h:raise ValueError('model per-cell identity')
                inputs[p]=h
        save(run.output/'input_hashes.json',inputs);save(run.output/'cases.json',cases)
        save(run.output/'registration.json',{'intervention':'segment_budget8->16 only','cells':sorted(cases),
             'minimum_reduced_cells':24,'maximum_payload_ratio':0.75,'maximum_added_directory_steps':0.5,
             'protocol_sha256':sha(PROTOCOL),'timing_claims':False})
        run.report['builds']={}
        for name,sanitize in (('opt',False),('sanitizer',True)):
            binary=run.build('model16_'+name,[DIAG/'bm_model16_work.cpp',model],sanitize,definitions)
            result={}
            for cell,meta in sorted(cases.items()):
                raw=run.execute([binary,'profile',*meta['paths']],f'{name}_{cell}.jsonl',
                                dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'))
                rows=[json.loads(line) for line in raw.splitlines()]
                if len(rows)!=4 or {r['arm'] for r in rows}!={'tx8_dynamic','m16_dynamic','tx8_snapshot','m16_snapshot'}:
                    raise ValueError('model profile coverage')
                result[cell]=rows;run.report['builds'][name]=result;save(run.output/'report.json',run.report)
            print(name+': model16 work32 complete',flush=True)
        if run.report['builds']['opt']!=run.report['builds']['sanitizer']:raise ValueError('model work differs across builds')
        run.report['nomination']=nominate(run.report['builds']['opt'])
        if any(sha(p)!=h for p,h in inputs.items()):raise ValueError('model input drift')
        run.finish();print(json.dumps({'status':'complete','scopes':{k:{'nominate':v['nominate'],'reduced_cells':v['reduced_cells']} for k,v in run.report['nomination'].items()}}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

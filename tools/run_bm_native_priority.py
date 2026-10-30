"""One own-thread priority intervention; unchanged original native AA and contrasts."""
from pathlib import Path
import argparse
import json
from run_bm_tx import ROOT,DIAG,save,sha
import run_bm_query_native as native

PROTOCOL=ROOT/'docs/PROTOCOL_BM_NATIVE_PRIORITY_20261030.md'

def decode_priority(output):
    lines=output.splitlines()
    start=[x.removeprefix('NATIVE_PRIORITY_START ') for x in lines if x.startswith('NATIVE_PRIORITY_START ')]
    end=[x.removeprefix('NATIVE_PRIORITY_END ') for x in lines if x.startswith('NATIVE_PRIORITY_END ')]
    if len(start)!=1 or len(end)!=1:raise ValueError('priority metadata coverage')
    a,b=json.loads(start[0]),json.loads(end[0])
    if (a['requested']!=1 or a['actual']!=1 or b['actual']!=1 or a['process_class']!=b['process_class']
            or not b['restored'] or b['restored_actual']!=a['previous']):
        raise ValueError('priority not applied/restored or process class changed')
    payload='\n'.join(x for x in lines if not x.startswith(('NATIVE_PRIORITY_START ','NATIVE_PRIORITY_END ')))+'\n'
    return payload,{'start':a,'end':b}

class PriorityRun(native.NativeRun):
    def execute(self,args,name,env=None):
        output=super().execute(args,name,env)
        if 'NATIVE_PRIORITY_START ' in output or 'NATIVE_PRIORITY_END ' in output:
            payload,record=decode_priority(output)
            self.report.setdefault('priority_records',{})[name]=record
            return payload
        if 'query_priority' in str(args[0]) and str(args[0]) in self.binaries:
            raise ValueError('missing priority metadata')
        return output

def read_block(path):
    # Reanalysis always uses preserved raw bytes, including validated priority records.
    payload,_=decode_priority(path.read_text());return json.loads(payload)

def selection(path,digest,hashes,affinity,process_class):
    chosen=json.loads(path.read_text());directory=path.resolve().parent
    report=json.loads((directory/'report.json').read_text())
    if (report.get('status')!='complete' or report.get('phase')!='calibrate'
        or chosen['report_sha256']!=sha(directory/'report.json') or chosen['source_closure_sha256']!=digest
        or chosen['protocol_sha256']!=sha(PROTOCOL) or chosen['selected_ms']!=1000
        or json.loads((directory/'input_hashes.json').read_text())!=hashes
        or chosen['affinity']!=list(affinity) or chosen['process_class']!=process_class):
        raise ValueError('unqualified or unbound priority selection')
    if set(chosen['sample_hashes'])!={f'{cell}_p{p:02d}.json' for cell in native.PILOT for p in range(4)}:
        raise ValueError('priority calibration sample coverage')
    for name,digest in chosen['sample_hashes'].items():
        if sha(directory/name)!=digest:raise ValueError('priority calibration raw-record drift')
    for cell in native.PILOT:
        blocks=[read_block(directory/f'{cell}_p{p:02d}.json') for p in range(4)]
        actual=native.analyze(blocks)
        if actual!=report['results'][cell] or not actual['all_aa_pass']:raise ValueError('priority AA reanalysis')
    return chosen

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--phase',choices=('calibrate','confirm'),default='calibrate')
    parser.add_argument('--selection',type=Path)
    parser.add_argument('--compile-only',action='store_true')
    args=parser.parse_args();run=PriorityRun(args.output)
    for p in (Path(__file__).resolve(),PROTOCOL,Path(native.__file__).resolve(),
              Path(native.old_statistics.__file__).resolve()):run.sources[str(p)]=sha(p)
    try:
        cases,hashes=native.frozen_inputs();save(run.output/'cases.json',cases);save(run.output/'input_hashes.json',hashes)
        definitions={'BM_TX_CONTROL_BM1':'1','BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp')}
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        binary=run.build('query_priority',[DIAG/'bm_query_native_priority.cpp',model],definitions=definitions)
        contract_source=run.output/'contract.cpp'
        contract_source.write_bytes(b'#include "bm_native_runtime.hpp"\n'
            b'#define ACCEPTANCE_FACTORY_HEADER "bm_tx_adapter.hpp"\n'
            b'#define ACCEPTANCE_FACTORY_SYMBOL make_bm_tx_experiment_index\n'
            b'#define main bm_priority_contract_entry\n#include "borrowed_contract_acceptance.cpp"\n#undef main\n'
            b'int main(){try{int status=bm_priority_contract_entry();if(status)return status;'
            b'std::cout<<"NATIVE_RUNTIME ";bm_native_runtime::emit_modules();std::cout<<std::endl;return 0;}'
            b'catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}\n')
        contract=run.build('contract_priority',[contract_source,model])
        if args.compile_only:
            run.report.update(phase='compile-only');run.finish();print('priority compile-only complete',flush=True);return
        run.report['contract']=run.execute([contract],'contract.log')
        if '99/99 checks passed' not in run.report['contract']:raise ValueError('priority public contract')
        modules=[line.removeprefix('NATIVE_RUNTIME ') for line in run.report['contract'].splitlines() if line.startswith('NATIVE_RUNTIME ')]
        if len(modules)!=1:raise ValueError('priority contract runtime')
        run.verify_modules({'runtime_modules':json.loads(modules[0])})
        run.report.update(phase=args.phase,semantic={},results={});affinity=set()
        for cell,paths in sorted(cases.items()):
            record=json.loads(run.execute([binary,'check',*paths,0,1000],cell+'_check.json'));run.verify_modules(record)
            if not record['passed']:raise ValueError('priority semantics')
            run.report['semantic'][cell]=record;affinity.add((record['cpu'],record['affinity']))
        if len(affinity)!=1:raise ValueError('priority affinity drift')
        affinity=next(iter(affinity));digest=native.closure_digest(run)
        classes={x['start']['process_class'] for x in run.report['priority_records'].values()}
        if len(classes)!=1:raise ValueError('priority process class drift')
        process_class=next(iter(classes))
        if args.phase=='confirm':
            if args.selection is None:raise ValueError('priority selection required')
            save(run.output/'selection.json',selection(args.selection,digest,hashes,affinity,process_class))
        cells=native.PILOT if args.phase=='calibrate' else tuple(sorted(cases));processes=4 if args.phase=='calibrate' else 16
        save(run.output/'registration.json',{'phase':args.phase,'floor_ms':1000,'cells':cells,'processes':processes,
             'rounds':8,'affinity':list(affinity),'process_class':process_class,'own_thread_priority':1,'protocol_sha256':sha(PROTOCOL),
             'source_closure_sha256':digest,'estimator':'unchanged native parity and halves AA'})
        for cell in cells:
            blocks=[]
            for process in range(processes):
                record=json.loads(run.execute([binary,'run',*cases[cell],process,1000],f'{cell}_p{process:02d}.json'))
                run.verify_modules(record)
                if run.report['priority_records'][f'{cell}_p{process:02d}.json']['start']['process_class']!=process_class:
                    raise ValueError('priority measured process class changed')
                if (record['cpu'],record['affinity'])!=affinity:raise ValueError('priority measured affinity')
                blocks.append(record)
            run.report['results'][cell]=native.analyze(blocks,args.phase=='confirm')
            save(run.output/'report.json',run.report);print(cell+': AA='+str(run.report['results'][cell]['all_aa_pass']),flush=True)
        if args.phase=='calibrate':
            run.report['selection']={'selected_ms':1000 if all(x['all_aa_pass'] for x in run.report['results'].values()) else None,
                'affinity':list(affinity),'process_class':process_class,'protocol_sha256':sha(PROTOCOL),'source_closure_sha256':digest,
                'sample_hashes':{f'{cell}_p{p:02d}.json':sha(run.output/f'{cell}_p{p:02d}.json') for cell in native.PILOT for p in range(4)},
                'decision':'AA only'}
        if any(sha(p)!=h for p,h in hashes.items()):raise ValueError('priority input drift at completion')
        run.finish()
        if args.phase=='calibrate':save(run.output/'selection.json',dict(run.report['selection'],report_sha256=sha(run.output/'report.json')))
        print(json.dumps({'status':'complete','selection':run.report.get('selection')}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

"""Known-truth relative snapshot qualification, then one bound Model16 comparison."""
from pathlib import Path
import argparse
import datetime as dt
import json
import os
import subprocess
import sys
import time
from run_bm_tx import ROOT,DIAG,save,sha
import run_bm_query_native as native
import run_bm_model16_work as model16
import bm_paired_stats as stats

PROTOCOL=ROOT/'docs/PROTOCOL_BM_PAIRED_SNAPSHOT_20261030.md'

class PairedRun(native.NativeRun):
    def execute(self,args,name,env=None):
        args=list(map(str,args))
        if args[0] not in self.binaries:return super().execute(args,name,env)
        if sha(args[0])!=self.binaries[args[0]] or any(sha(p)!=h for p,h in self.runtime.items()):
            raise ValueError('paired binary/runtime drift')
        self.commands.append(args);save(self.output/'commands.json',self.commands)
        record={'start_utc':dt.datetime.now(dt.timezone.utc).isoformat(),'timeout_seconds':900}
        self.report.setdefault('children',{})[name]=record;start=time.monotonic()
        try:
            with (self.output/name).open('w') as stream:
                result=subprocess.run(args,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,
                                      timeout=900,creationflags=subprocess.NORMAL_PRIORITY_CLASS)
            record['returncode']=result.returncode
            if result.returncode:raise RuntimeError(name+': exit '+str(result.returncode))
        except subprocess.TimeoutExpired:
            record['own_child_timeout']=True;raise RuntimeError('paired own child timed out: '+name)
        finally:
            record.update(end_utc=dt.datetime.now(dt.timezone.utc).isoformat(),seconds=time.monotonic()-start)
            save(self.output/'report.json',self.report)
        return (self.output/name).read_text()

def native_header(run):
    source=DIAG/'bm_query_native.cpp'
    frozen=subprocess.check_output(['git','-C',str(ROOT),'show','d337576:'+source.relative_to(ROOT).as_posix()])
    if source.read_bytes()!=frozen:raise ValueError('paired native preimage drift')
    text=frozen.decode();entry='int main(int argc,char** argv)';success='std::cout<<"]}\\n";\n    }catch'
    if text.count(entry)!=1 or text.count(success)!=1:raise ValueError('paired native entry/return anchors')
    text=text.replace(entry,'static int bm_paired_unused_native_entry(int argc,char** argv)',1)
    text=text.replace(success,'std::cout<<"]}\\n";return 0;\n    }catch',1)
    target=run.output/'paired_native_helpers.hpp';target.write_bytes(text.encode());run.sources[str(source)]=sha(source)
    save(run.output/'native_generation.json',{'base':'d337576','preimage_sha256':sha(source),
        'generated_sha256':sha(target),'changes':['entry rename','explicit successful main return']})
    return target

def public_contract(run,name,header,symbol,model,definitions,count):
    source=run.output/(name+'.cpp')
    lines=['#include "bm_native_runtime.hpp"',f'#define ACCEPTANCE_FACTORY_HEADER "{header}"',
           '#define ACCEPTANCE_FACTORY_SYMBOL '+symbol,'#define main paired_contract_entry',
           '#include "borrowed_contract_acceptance.cpp"','#undef main',
           'int main(){try{const auto rc=paired_contract_entry();if(rc)return rc;',
           'std::cout<<"NATIVE_RUNTIME ";bm_native_runtime::emit_modules();std::cout<<std::endl;return 0;',
           '}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}']
    source.write_bytes(('\n'.join(lines)+'\n').encode())
    binary=run.build(name,[source,model],definitions=definitions)
    return binary,count

def select(path,digest,hashes,affinity):
    chosen=json.loads(path.read_text());directory=path.resolve().parent
    report=json.loads((directory/'report.json').read_text())
    expected={f'{cell}_p{p:02d}.json' for cell in native.PILOT for p in range(4)}
    if (report.get('status')!='complete' or report.get('phase')!='calibrate' or not chosen['qualified']
        or chosen['report_sha256']!=sha(directory/'report.json') or chosen['source_closure_sha256']!=digest
        or chosen['protocol_sha256']!=sha(PROTOCOL) or chosen['affinity']!=list(affinity)
        or set(chosen['sample_hashes'])!=expected or json.loads((directory/'input_hashes.json').read_text())!=hashes):
        raise ValueError('unqualified or unbound paired selection')
    for name,h in chosen['sample_hashes'].items():
        if sha(directory/name)!=h:raise ValueError('paired raw calibration drift')
    for cell in native.PILOT:
        blocks=[json.loads((directory/f'{cell}_p{p:02d}.json').read_text()) for p in range(4)]
        actual=stats.analyze(blocks,True)
        if actual!=report['results'][cell] or not actual['quality_pass']:raise ValueError('paired selection reanalysis')
    return chosen

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--phase',choices=('calibrate','confirm'),default='calibrate')
    parser.add_argument('--selection',type=Path)
    parser.add_argument('--compile-only',action='store_true')
    args=parser.parse_args()
    prior=ROOT/'07_Eval_Integration/results/bm_native_idle_calibration_20261030/report.json'
    raw=subprocess.check_output(['git','-C',str(ROOT),'show','ed3f73f:'+prior.relative_to(ROOT).as_posix()])
    if prior.read_bytes()!=raw:raise ValueError('prior idle report preimage drift')
    previous=json.loads(raw);end=dt.datetime.fromisoformat(previous['window']['confirmed_end_utc'])
    if previous['status']!='complete' or dt.datetime.now(dt.timezone.utc)<end:
        raise ValueError('finish and leave prior idle window before new method')
    run=PairedRun(args.output)
    for p in (Path(__file__).resolve(),PROTOCOL,Path(stats.__file__).resolve(),Path(native.__file__).resolve(),
              native.PROTOCOL,Path(native.old_statistics.__file__).resolve(),native.old_statistics.PROTOCOL,
              Path(model16.__file__).resolve(),model16.PROTOCOL,Path(sys.executable).resolve(),prior):
        run.sources[str(p)]=sha(p)
    try:
        run.report['synthetic_analysis_checks']=stats.self_check()
        generated=model16.generate(run)
        accepted=json.loads((ROOT/'07_Eval_Integration/results/bm_model16_work_20261030/generation.json').read_text())
        actual=json.loads((run.output/'generation.json').read_text())
        if {k:v['generated_sha256'] for k,v in accepted.items()}!={k:v['generated_sha256'] for k,v in actual.items()}:
            raise ValueError('paired Model16 differs from accepted source')
        helpers=native_header(run)
        definitions={'BM_MODEL16_INDEX_HEADER':generated.as_posix(),'BM_PAIR_NATIVE_HEADER':helpers.as_posix(),
             'BM_TX_CONTROL_BM1':'1','BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp'),
             'BM_MODEL16_ADAPTER_SHA256':sha(DIAG/'bm_model16_snapshot_adapter.hpp')}
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        binary=run.build('snapshot_paired',[DIAG/'bm_snapshot_paired.cpp',model],definitions=definitions)
        contracts=[public_contract(run,'contract_tx8','bm_tx_adapter.hpp','make_bm_tx_experiment_index',model,definitions,99),
            public_contract(run,'contract_m16','bm_model16_snapshot_adapter.hpp','make_bm_model16_snapshot_index',model,definitions,127)]
        if args.compile_only:
            run.report['phase']='compile-only';run.finish();print('paired compile-only complete',flush=True);return
        run.report['contracts']={}
        for contract,count in contracts:
            text=run.execute([contract],contract.stem+'.log')
            if f'{count}/{count} checks passed' not in text:raise ValueError('paired public contract')
            modules=[line.removeprefix('NATIVE_RUNTIME ') for line in text.splitlines() if line.startswith('NATIVE_RUNTIME ')]
            if len(modules)!=1:raise ValueError('paired contract runtime metadata')
            run.verify_modules({'runtime_modules':json.loads(modules[0])});run.report['contracts'][contract.stem]=text
        cases,hashes=native.frozen_inputs();save(run.output/'cases.json',cases);save(run.output/'input_hashes.json',hashes)
        run.report.update(phase=args.phase,semantic={},results={},scope='relative snapshot only; old absolute gates unchanged')
        affinity=set()
        for cell,paths in sorted(cases.items()):
            record=json.loads(run.execute([binary,'check',*paths,0,1000,args.phase],cell+'_check.json'));run.verify_modules(record)
            if not record['passed'] or record['phase']!=args.phase or record['model16_measured']!=(args.phase=='confirm'):
                raise ValueError('paired phase semantic role')
            run.report['semantic'][cell]=record;affinity.add((record['cpu'],record['affinity']))
        if len(affinity)!=1:raise ValueError('paired semantic affinity')
        affinity=next(iter(affinity))
        if list(affinity)!=previous['selection']['affinity']:raise ValueError('paired CPU shopping forbidden')
        digest=native.closure_digest(run)
        if args.phase=='confirm':
            if args.selection is None:raise ValueError('paired known-truth selection required')
            chosen=select(args.selection,digest,hashes,affinity);save(run.output/'selection.json',chosen)
            run.report['known_truth_selection']=chosen
        cells=native.PILOT if args.phase=='calibrate' else tuple(sorted(cases));processes=4 if args.phase=='calibrate' else 16
        save(run.output/'registration.json',{'phase':args.phase,'scope':'relative snapshots','fixed_floor_ms':1000,
             'quads':8,'batches':32,'processes':processes,'cells':cells,'affinity':affinity,
             'source_closure_sha256':digest,'protocol_sha256':sha(PROTOCOL),'old_absolute_gate_reclassified':False})
        for cell in cells:
            blocks=[]
            for process in range(processes):
                record=json.loads(run.execute([binary,'run',*cases[cell],process,1000,args.phase],f'{cell}_p{process:02d}.json'))
                run.verify_modules(record);blocks.append(record)
            run.report['results'][cell]=stats.analyze(blocks,args.phase=='calibrate')
            save(run.output/'report.json',run.report);print(cell+': quality='+str(run.report['results'][cell]['quality_pass']),flush=True)
        if args.phase=='calibrate':
            run.report['selection']={'qualified':all(v['quality_pass'] for v in run.report['results'].values()),
                'source_closure_sha256':digest,'protocol_sha256':sha(PROTOCOL),'affinity':affinity,'scope':'relative snapshots',
                'sample_hashes':{f'{c}_p{p:02d}.json':sha(run.output/f'{c}_p{p:02d}.json') for c in native.PILOT for p in range(4)}}
        else:
            values=run.report['results'].values();gains=sum(v['verdict']=='gain_at_least10pct' for v in values)
            guarded=all(v['quality_pass'] and v['primary_relative']['ci95'][1]<=1.05 for v in values)
            run.report['relative_read_target']={'gained_cells':gains,'all_quality_and_noregression':guarded,
                 'pass':guarded and gains>=24,'old_absolute_gate_reclassified':False,'default_adoption':False}
        if any(sha(p)!=h for p,h in hashes.items()):raise ValueError('paired input drift at completion')
        run.finish()
        if args.phase=='calibrate':save(run.output/'selection.json',dict(run.report['selection'],report_sha256=sha(run.output/'report.json')))
        print(json.dumps({'status':'complete','phase':args.phase,'selection':run.report.get('selection')}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

"""Qualify Windows-native elapsed-time measurements without pooling WSL samples."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import platform
import shutil
import statistics
import subprocess
import sys
import run_bm_query_stability as old_statistics
from run_bm_tx import Run,ROOT,DIAG,save,sha

PROTOCOL=ROOT/'docs/PROTOCOL_BM_NATIVE_QUERY_20261030.md'
PILOT=('words_95_5_uniform_growth','urls_80_20_hotspot_replacement')
STATES=('bm_dynamic','candidate_dynamic','bm_snapshot','candidate_snapshot')

class NativeRun(Run):
    def __init__(self,output):
        if sys.platform!='win32':raise ValueError('native Windows execution required')
        super().__init__(output)
        self.flags=[f for f in self.flags if f!='-pthread']
        self.compiler=Path(shutil.which('g++') or '').resolve()
        if self.compiler!=Path('D:/DevTools/mingw64/bin/g++.exe'):raise ValueError('unregistered native compiler')
        version=subprocess.check_output([str(self.compiler),'-dumpfullversion'],text=True).strip()
        if version!='15.2.0':raise ValueError('native compiler version drift')
        programs=[self.compiler]
        for query in ('cc1plus','collect2','ld','as'):
            named=subprocess.check_output([str(self.compiler),'-print-prog-name='+query],text=True).strip()
            p=Path(named)
            if not p.is_absolute():p=Path(shutil.which(named) or shutil.which(named+'.exe') or (self.compiler.parent/named))
            p=p.resolve()
            if not p.is_file():raise ValueError('unbound compiler helper: '+query)
            programs.append(p)
        libraries=[p for p in (self.compiler.parent/n for n in ('libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll')) if p.is_file()]
        programs.extend(libraries);self.runtime={}
        for source in libraries:
            staged=self.output/source.name;partial=staged.with_name(staged.name+'.partial')
            shutil.copyfile(source,partial);os.replace(partial,staged)
            self.runtime[str(staged)]=sha(source)
            if sha(staged)!=self.runtime[str(staged)]:raise ValueError('runtime staging identity')
        self.sources.update(self.runtime)
        self.sources.update({str(p):sha(p) for p in programs})
        save(self.output/'environment.json',{'platform':platform.platform(),'python':sys.version,
             'compiler':str(self.compiler),'version':version,'toolchain':{str(p):sha(p) for p in programs},'staged_runtime':self.runtime,
             'timing':'native wall; CPU time and cycles diagnostic only'})

    def execute(self,args,name,env=None):
        args=list(args)
        if any(sha(p)!=h for p,h in self.runtime.items()):raise ValueError('native runtime drift before execution')
        if str(args[0])=='g++':args[0]=str(self.compiler)
        if str(args[0])==str(self.compiler) and '-o' in args:
            output_position=args.index('-o')+1;requested=Path(args[output_position])
            linked=requested.with_name(requested.name+'.exe');args[output_position]=linked
            result=super().execute(args,name,env)
            os.replace(linked,requested)
            return result
        result=super().execute(args,name,env)
        # Makefile output may escape a Windows drive colon; the raw log stays intact.
        return result.replace('\\:',':') if '-MM' in args else result

    def build(self,name,*args,**kwargs):return super().build(name+'.exe',*args,**kwargs)

    def verify_modules(self,record):
        for name,location in record['runtime_modules'].items():
            expected=(self.output/name).resolve();actual=Path(location).resolve()
            if actual!=expected or str(expected) not in self.runtime or sha(actual)!=self.runtime[str(expected)]:
                raise ValueError('unexpected loaded runtime module')

def closure_digest(run):
    normalized={}
    for name,digest in run.sources.items():
        path=Path(name)
        if path.is_relative_to(run.output):key='run-output/'+path.relative_to(run.output).as_posix()
        elif path.is_relative_to(ROOT):key='workspace/'+path.relative_to(ROOT).as_posix()
        else:key=path.as_posix()
        normalized[key]=digest
    return hashlib.sha256(json.dumps(normalized,sort_keys=True).encode()).hexdigest()

def analyze(blocks,contrasts=False):
    for process,block in enumerate(blocks):
        rows=block['measurements']
        if (block['process']!=process or len(rows)!=32 or
            {(r['state'],r['round']) for r in rows}!={(s,n) for s in STATES for n in range(8)}):
            raise ValueError('incomplete native block')
        if len({r['lookups'] for r in rows})!=1 or len({r['checksum'] for r in rows})!=1:raise ValueError('native checksum/loop identity')
        if any(r['cpu_before']!=block['cpu'] or r['cpu_after']!=block['cpu'] for r in rows):raise ValueError('native migration')
    if len({(b['cpu'],b['affinity']) for b in blocks})!=1:raise ValueError('native affinity changed')
    def paired(numerator,denominator):
        return old_statistics.interval([statistics.fmean(math.log(r['ns']/r['lookups']) for r in b['measurements'] if numerator(r))-
                                        statistics.fmean(math.log(r['ns']/r['lookups']) for r in b['measurements'] if denominator(r)) for b in blocks])
    result={'aa':{},'diagnostics':{}}
    for state in STATES:
        checks={}
        for name,predicate in (('parity',lambda r:r['round']%2==1),('halves',lambda r:r['round']>=4)):
            value=paired(lambda r:r['state']==state and predicate(r),lambda r:r['state']==state and not predicate(r))
            value['pass']=value['ci95'][0]>=1/1.05 and value['ci95'][1]<=1.05;checks[name]=value
        result['aa'][state]={'checks':checks,'pass':all(v['pass'] for v in checks.values())}
        records=[r for b in blocks for r in b['measurements'] if r['state']==state]
        result['diagnostics'][state]={'cycles_per_lookup_range':[min(r['cycles']/r['lookups'] for r in records),max(r['cycles']/r['lookups'] for r in records)],
             'cpu_time_zero_batches':sum(r['cpu_ns']==0 for r in records),'processor_migrations':0,
             'unavailable':['context_switch_counts','page_fault_counts'],
             'cycles_are_not_elapsed_time':True}
    result['all_aa_pass']=all(v['pass'] for v in result['aa'].values())
    if contrasts:
        result['contrasts']={}
        for name,candidate,control,limit in (
            ('candidate_vs_bm1_dynamic','candidate_dynamic','bm_dynamic',1.05),
            ('candidate_vs_bm1_snapshot','candidate_snapshot','bm_snapshot',1.05),
            ('candidate_dynamic_vs_snapshot','candidate_dynamic','candidate_snapshot',1.15),
            ('bm1_dynamic_vs_snapshot','bm_dynamic','bm_snapshot',1.15)):
            value=paired(lambda r:r['state']==candidate,lambda r:r['state']==control)
            quality=result['aa'][candidate]['pass'] and result['aa'][control]['pass']
            verdict='pass' if value['ci95'][1]<=limit else ('regression' if value['ci95'][0]>limit else 'undecided')
            value.update(limit=limit,aa_quality_pass=quality,performance_interval_verdict=verdict,
                         verdict=verdict if quality else 'measurement_unresolved')
            result['contrasts'][name]=value
    return result

def frozen_inputs():
    manifests={}
    for name in ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025'):
        manifests.update(json.loads((ROOT/'07_Eval_Integration/results'/name/'manifest.json').read_text()))
    historical=json.loads((ROOT/'07_Eval_Integration/results/bm_query_recheck_20261024/cases.json').read_text())
    old_cases=json.loads((ROOT/'builds/bm_tx_query_bm1_20261030/cases.json').read_text())
    if len(manifests)!=32 or set(old_cases)!=set(manifests):raise ValueError('native cell coverage')
    cases={};hashes={};prefix='/mnt/d/StringLearnedIndex_Workspace/'
    for cell,meta in manifests.items():
        old=historical[cell];pool=old_cases[cell]['paths'][2]
        if not pool.startswith(prefix):raise ValueError('pool location')
        pool_path=(ROOT/pool[len(prefix):]).resolve()
        if not pool_path.is_relative_to(ROOT):raise ValueError('pool outside workspace')
        directory=ROOT/'builds/bm_tx_query_bm1_20261030/cases'/cell
        paths=[ROOT/meta['rows_path'],ROOT/meta['operations_path'],pool_path,directory/'final.bin',directory/'queries.bin']
        expected=[meta['rows_sha256'],meta['operations_sha256'],old['input_sha256']['pool'],old['final_sha256'],old['query_sha256']]
        for path,digest in zip(paths,expected):
            if sha(path)!=digest:raise ValueError('native per-cell input identity')
            hashes[str(path)]=digest
        cases[cell]=list(map(str,paths))
    return cases,hashes

def choose(results):
    if set(results)!={'250','1000'} or any(set(x)!=set(PILOT) for x in results.values()):raise ValueError('pilot coverage')
    floors=[int(f) for f,cells in results.items() if all(v['all_aa_pass'] for v in cells.values())]
    return min(floors) if floors else None

def validate_selection(path,source_digest,hashes,affinity):
    selection=json.loads(path.read_text());directory=path.resolve().parent
    report=json.loads((directory/'report.json').read_text())
    if (report.get('status')!='complete' or report.get('phase')!='calibrate'
            or sha(directory/'report.json')!=selection['report_sha256']
            or source_digest!=selection['source_closure_sha256']
            or sha(PROTOCOL)!=selection['protocol_sha256']
            or json.loads((directory/'input_hashes.json').read_text())!=hashes
            or list(affinity)!=selection['affinity']):raise ValueError('unbound native selection')
    for floor in (250,1000):
        for cell in PILOT:
            blocks=[json.loads((directory/f'{floor}_{cell}_p{p:02d}.json').read_text()) for p in range(4)]
            if analyze(blocks)!=report['results'][str(floor)][cell]:raise ValueError('native AA analysis drift')
    selected=choose(report['results'])
    if selected is None or selected!=selection['selected_ms']:raise ValueError('native calibration did not qualify')
    return selection

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--phase',choices=('calibrate','confirm'),default='calibrate')
    parser.add_argument('--selection',type=Path)
    args=parser.parse_args();run=NativeRun(args.output)
    run.sources.update({str(Path(__file__).resolve()):sha(Path(__file__)),str(PROTOCOL):sha(PROTOCOL),
                        str(Path(old_statistics.__file__).resolve()):sha(old_statistics.__file__)})
    try:
        cases,hashes=frozen_inputs();save(run.output/'cases.json',cases);save(run.output/'input_hashes.json',hashes)
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        definitions={'BM_TX_CONTROL_BM1':'1','BM_QUERY_BM1_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp')}
        binary=run.build('query_native',[DIAG/'bm_query_native.cpp',model],definitions=definitions)
        contract_source=run.output/'contract.cpp'
        contract_lines=['#include "bm_native_runtime.hpp"',
            '#define ACCEPTANCE_FACTORY_HEADER "bm_tx_adapter.hpp"',
            '#define ACCEPTANCE_FACTORY_SYMBOL make_bm_tx_experiment_index',
            '#define main bm_native_contract_entry','#include "borrowed_contract_acceptance.cpp"','#undef main',
            'int main(){try{const int status=bm_native_contract_entry();if(status)return status;',
            'std::cout<<"NATIVE_RUNTIME ";bm_native_runtime::emit_modules();std::cout<<std::endl;return 0;',
            '}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}']
        contract_source.write_bytes((chr(10).join(contract_lines)+chr(10)).encode())
        contract=run.build('contract_native',[contract_source,model]);run.report['contract']=run.execute([contract],'contract.log')
        if '99/99 checks passed' not in run.report['contract']:raise ValueError('native public contract')
        module_lines=[line.removeprefix('NATIVE_RUNTIME ') for line in run.report['contract'].splitlines() if line.startswith('NATIVE_RUNTIME ')]
        if len(module_lines)!=1:raise ValueError('native contract runtime record')
        run.verify_modules({'runtime_modules':json.loads(module_lines[0])})
        run.report.update(phase=args.phase,semantic={},results={})
        affinity=set()
        for cell,paths in sorted(cases.items()):
            check=json.loads(run.execute([binary,'check',*paths,0,250],cell+'_check.json'))
            run.verify_modules(check)
            if not check['passed']:raise ValueError('native semantic gate')
            run.report['semantic'][cell]=check;affinity.add((check['cpu'],check['affinity']))
        if len(affinity)!=1:raise ValueError('semantic native affinity changed')
        affinity=next(iter(affinity));digest=closure_digest(run)
        if args.phase=='confirm':
            if args.selection is None:raise ValueError('native selection required')
            selection=validate_selection(args.selection,digest,hashes,affinity);floors=[selection['selected_ms']]
            save(run.output/'selection.json',selection)
        else:floors=[250,1000]
        selected_cells=PILOT if args.phase=='calibrate' else tuple(sorted(cases));processes=4 if args.phase=='calibrate' else 16
        save(run.output/'registration.json',{'phase':args.phase,'floors':floors,'cells':selected_cells,'processes':processes,
             'rounds':8,'affinity':list(affinity),'protocol_sha256':sha(PROTOCOL),'source_closure_sha256':digest})
        save(run.output/'report.json',run.report);print('Native contract99 and semantic32 complete',flush=True)
        for floor in floors:
            values={}
            for cell in selected_cells:
                blocks=[]
                for process in range(processes):
                    block=json.loads(run.execute([binary,'run',*cases[cell],process,floor],f'{floor}_{cell}_p{process:02d}.json'))
                    run.verify_modules(block)
                    if (block['cpu'],block['affinity'])!=affinity:raise ValueError('measured native affinity changed')
                    blocks.append(block)
                values[cell]=analyze(blocks,args.phase=='confirm');run.report['results'][str(floor)]=values
                save(run.output/'report.json',run.report);print(f"native {floor}ms {cell}: AA={values[cell]['all_aa_pass']}",flush=True)
        if args.phase=='calibrate':
            run.report['selection']={'selected_ms':choose(run.report['results']),'affinity':list(affinity),
                 'protocol_sha256':sha(PROTOCOL),'source_closure_sha256':digest,'decision':'AA only'}
        if any(sha(p)!=h for p,h in hashes.items()):raise ValueError('native input drift at completion')
        run.finish()
        if args.phase=='calibrate':save(run.output/'selection.json',dict(run.report['selection'],report_sha256=sha(run.output/'report.json')))
        print(json.dumps({'status':'complete','selection':run.report.get('selection')}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report);raise

if __name__=='__main__':main()

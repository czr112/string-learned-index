"""Bounded BM transaction experiments: immutable run directories and real build identities."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
DIAG=ROOT/'07_Eval_Integration/diagnostics'
PINS={'model':'3894c767338a865d2920ba5e21283827fea1d803',
      'benchmark':'003a55bf674b43350fbb70242070fb74f33a56ce'}

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,value):p.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

class Run:
    def __init__(self,output):
        self.output=output.resolve();self.output.mkdir(parents=True,exist_ok=False)
        self.commands=[];self.binaries={};self.sources={str(Path(__file__).resolve()):sha(Path(__file__))}
        self.report={'status':'running','binaries':self.binaries}
        self.includes=[DIAG,ROOT/'02_Code_and_Experiments/validation_e25_20260918']
        for name,pin in PINS.items():
            dep=ROOT/'07_Eval_Integration/upstream_snapshots'/f'{name}_{pin[:8]}'
            actual=subprocess.check_output(['git','-C',str(dep),'rev-parse','HEAD'],text=True).strip()
            if actual!=pin:raise RuntimeError(f'{name}: unexpected pin {actual}')
            subprocess.run(['git','-C',str(dep),'diff','--exit-code','HEAD','--','src'],check=True,stdout=subprocess.DEVNULL)
            self.includes.append(dep/'src')
        self.flags=['-std=c++17','-pthread','-march=x86-64-v3',*[f'-I{x}' for x in self.includes]]

    def execute(self,args,name,env=None):
        args=list(map(str,args))
        for token in args:
            if token in self.binaries and sha(token)!=self.binaries[token]:raise RuntimeError('binary drift')
        self.commands.append(args);save(self.output/'commands.json',self.commands)
        with (self.output/name).open('w') as stream:
            result=subprocess.run(args,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,env=env)
        if result.returncode:raise RuntimeError(f'{name}: exit {result.returncode}; output retained')
        return (self.output/name).read_text()

    def build(self,name,sources,sanitize=False,definitions=None,objects=()):
        definitions=dict(definitions or {})
        definitions.update({'BM_MODEL_COMMIT':PINS['model'],
                            'BM_TX_CORE_SHA256':'dependency-scan','BM_TX_ADAPTER_SHA256':sha(DIAG/'bm_tx_adapter.hpp'),
                            'BM_SPLIT_CORE_SHA256':'dependency-scan','BM_SPLIT_ADAPTER_SHA256':sha(DIAG/'bm_split_adapter.hpp'),
                            'BM_CORE_BUNDLE_SHA256':'dependency-scan','BM_EXPERIMENT_ADAPTER_SHA256':sha(DIAG/'bm_experiment_adapter.hpp')})
        scan_macros=[f'-D{k}="{v}"' for k,v in definitions.items()]
        # Scan the actual include closure; hashes bind the source version, never a label.
        paths=set(map(Path,sources))
        for i,source in enumerate(sources):
            dependency=self.execute(['g++',*self.flags,*scan_macros,'-MM',source],f'{name}_deps_{i}.log')
            for token in dependency.replace('\\\n',' ').split()[1:]:
                p=Path(token)
                if not p.is_absolute():p=ROOT/p
                paths.add(p.resolve())
        hashes={str(p):sha(p) for p in paths}
        for p,h in hashes.items():
            if p in self.sources and self.sources[p]!=h:raise RuntimeError('source drift between builds')
        self.sources.update(hashes)
        bundle=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).hexdigest()
        definitions.update({'BM_MODEL_COMMIT':PINS['model'],'BM_TX_CORE_SHA256':bundle,
                            'BM_TX_ADAPTER_SHA256':sha(DIAG/'bm_tx_adapter.hpp'),'BM_SPLIT_CORE_SHA256':bundle,
                            'BM_CORE_BUNDLE_SHA256':bundle})
        macros=[f'-D{k}="{v}"' for k,v in definitions.items()]
        flags=['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if sanitize else ['-O3']
        staged=self.output/(name+'.partial');final=self.output/name
        self.execute(['g++',*self.flags,*flags,*macros,*sources,*objects,'-o',staged],name+'_build.log')
        os.replace(staged,final);self.binaries[str(final)]=sha(final)
        save(self.output/'identity.json',{'pins':PINS,'closure':self.sources,'runner_sha256':sha(Path(__file__)),
                                        'compiler':subprocess.check_output(['g++','--version'],text=True),
                                        'binaries':self.binaries})
        return final

    def finish(self):
        for p,h in self.sources.items():
            if sha(p)!=h:raise RuntimeError('source drift at completion')
        for p,h in self.binaries.items():
            if sha(p)!=h:raise RuntimeError('binary drift at completion')
        self.report['status']='complete';save(self.output/'report.json',self.report)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sanitize',action='store_true')
    parser.add_argument('--suite',choices=('regression','fault','traces','space','full'),default='regression')
    args=parser.parse_args()
    run=Run(args.output)
    try:
        model=ROOT/'07_Eval_Integration/upstream_snapshots/model_3894c767/src/block_model.cpp'
        binary=run.build('check',[DIAG/'check_bm_tx.cpp',model],args.sanitize)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:abort_on_error=1')
        run.report['regression']=json.loads(run.execute([binary],'regression.log',env))
        if args.suite=='full':
            contract_source=run.output/'contract.cpp'
            contract_source.write_text('#define ACCEPTANCE_FACTORY_HEADER "bm_tx_adapter.hpp"\n'
                                       '#define ACCEPTANCE_FACTORY_SYMBOL make_bm_tx_experiment_index\n'
                                       '#include "borrowed_contract_acceptance.cpp"\n')
            contract=run.build('contract',[contract_source,model],args.sanitize)
            run.report['contract']=run.execute([contract],'contract.log',env)
        if args.suite in ('fault','full'):
            run.report['fault']={}
            corpus=ROOT/'07_Eval_Integration/results/bm_real_20261018/interchange'
            for name,path in [('bytes','--bytes'),*[(n,corpus/f'{n}_rows.bin') for n in ('english_words','memetracker_urls','memetracker_phrases')]]:
                log=run.execute([binary,'fault',path],f'fault_{name}.jsonl',env)
                run.report['fault'][name]=[json.loads(x) for x in log.splitlines() if x.strip()]
                save(run.output/'report.json',run.report);print(f'fault {name}: complete',flush=True)
        if args.suite in ('traces','space','full'):
            run.report['cells']={}
            manifests=[ROOT/'07_Eval_Integration/results'/d/'manifest.json' for d in
                       ('bm_audited_v3_20261025','bm_audited_100k_v3_20261025')]
            for manifest in manifests:
                for cell,meta in sorted(json.loads(manifest.read_text()).items()):
                    rows=ROOT/meta['rows_path'];ops=ROOT/meta['operations_path']
                    if sha(rows)!=meta['rows_sha256'] or sha(ops)!=meta['operations_sha256']:raise RuntimeError('trace hash mismatch')
                    modes=('differential','space') if args.suite=='full' else ('space' if args.suite=='space' else 'differential',)
                    run.report['cells'][cell]={}
                    for mode in modes:
                        log=run.execute([binary,mode,rows,ops],cell+'_'+mode+'.jsonl',env)
                        run.report['cells'][cell][mode]=[json.loads(x) for x in log.splitlines() if x.strip()]
                        if mode=='space':
                            base,candidate=run.report['cells'][cell][mode]
                            run.report['cells'][cell]['terminal_ratio']=candidate['terminal']/base['terminal']
                            run.report['cells'][cell]['maintenance_peak_ratio']=candidate['maintenance_peak']/base['maintenance_peak']
                        save(run.output/'report.json',run.report);print(f'{mode} {cell}: complete',flush=True)
        run.finish();print(json.dumps({'status':'complete','output':str(run.output),'cells':len(run.report.get('cells',{}))}),flush=True)
    except Exception as error:
        run.report.update(status='failed',error=str(error));save(run.output/'report.json',run.report)
        raise

if __name__=='__main__':main()

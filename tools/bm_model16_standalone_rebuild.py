"""Self-contained source/contract rebuild for the private Model16 snapshot bundle."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT=Path(__file__).resolve().parent

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):
    temporary=path.with_name(path.name+'.partial')
    temporary.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8');os.replace(temporary,path)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--compiler',default='g++')
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    manifest_path=ROOT/'manifest.json';manifest_hash=sha(manifest_path)
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    report={'status':'running','sanitize':args.sanitize,'manifest_sha256':manifest_hash,'commands':[]}
    def unchanged():
        if sha(manifest_path)!=manifest_hash:raise ValueError('bundle manifest drift')
        for name,digest in manifest['files'].items():
            path=(ROOT/name).resolve()
            if not path.is_relative_to(ROOT) or sha(path)!=digest:raise ValueError('bundle source drift: '+name)
    env=dict(os.environ)
    removed=[name for name in ('CPATH','CPLUS_INCLUDE_PATH','C_INCLUDE_PATH','LIBRARY_PATH') if name in env]
    for name in removed:env.pop(name)
    report['removed_ambient_include_variables']=removed
    def execute(command,log):
        if any(sha(p)!=h for p,h in report.get('toolchain',{}).items()):raise ValueError('toolchain drift before execution')
        report['commands'].append(list(map(str,command)));save(output/'report.json',report)
        with (output/log).open('w',encoding='utf-8') as stream:
            result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT)
        if result.returncode:raise RuntimeError(log+': exit '+str(result.returncode))
        return (output/log).read_text(encoding='utf-8')
    try:
        unchanged();compiler=Path(shutil.which(args.compiler) or args.compiler).resolve()
        if not compiler.is_file():raise ValueError('compiler unavailable')
        compiler_hash=sha(compiler);report['toolchain']={str(compiler):compiler_hash}
        report['compiler']={'path':str(compiler),'sha256':compiler_hash,
                            'version':execute([compiler,'--version'],'compiler.log')}
        helpers={}
        for name in ('cc1plus','collect2','as','ld'):
            value=execute([compiler,'-print-prog-name='+name],name+'_location.log').strip()
            path=Path(value)
            if not path.is_absolute():path=Path(shutil.which(value) or value)
            path=path.resolve()
            if not path.is_file():raise ValueError('compiler helper unavailable: '+name)
            helpers[str(path)]=sha(path)
        report['toolchain']={**helpers,str(compiler):compiler_hash}
        definitions={'BM_MODEL_COMMIT':manifest['model_commit'],'BM_TX_CORE_SHA256':'dependency-scan',
                     'BM_MODEL16_ADAPTER_SHA256':manifest['files']['bm_model16_snapshot_adapter.hpp'],
                     'BM_MODEL16_INDEX_HEADER':'bm_m16_tx_index.hpp'}
        def macros():return [f'-D{k}="{v}"' for k,v in definitions.items()]
        flags=['-std=c++17','-march=x86-64-v3','-I.']+([] if os.name=='nt' else ['-pthread'])
        dependencies=set()
        for n,source in enumerate(manifest['compiled_sources']):
            raw=execute([compiler,*flags,*macros(),'-MM',source],f'dependencies_{n}.log')
            for token in raw.replace('\\\n',' ').split()[1:]:
                path=(ROOT/token).resolve()
                if not path.is_relative_to(ROOT):raise ValueError('dependency outside standalone bundle')
                name=path.relative_to(ROOT).as_posix()
                if name not in manifest['files']:raise ValueError('unlisted dependency: '+name)
                dependencies.add(name)
        if dependencies!=set(manifest['expected_closure']):raise ValueError('standalone dependency closure mismatch')
        closure={name:sha(ROOT/name) for name in sorted(dependencies)}
        definitions['BM_TX_CORE_SHA256']=hashlib.sha256(json.dumps(closure,sort_keys=True).encode()).hexdigest()
        report['source_closure']=closure;report['definitions']=definitions
        suffix='.exe' if os.name=='nt' else ''
        partial=output/('contract.partial'+suffix);binary=output/('contract'+suffix)
        optimization=['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-sanitize-recover=all'] if args.sanitize else ['-O3']
        execute([compiler,*flags,*optimization,*macros(),*manifest['compiled_sources'],'-o',partial],'build.log')
        os.replace(partial,binary);binary_hash=sha(binary);report['binary']={'path':str(binary),'sha256':binary_hash}
        unchanged()
        if sha(binary)!=binary_hash:raise ValueError('binary drift before execution')
        if args.sanitize:env['ASAN_OPTIONS']='detect_leaks=1:abort_on_error=1'
        result=execute([binary],'contract.log')
        if '127/127 checks passed' not in result:raise ValueError('standalone public contract did not pass127')
        unchanged()
        if sha(binary)!=binary_hash or any(sha(p)!=h for p,h in report['toolchain'].items()):
            raise ValueError('standalone binary/toolchain drift at completion')
        report.update(status='complete',contract_checks=127,workspace_dependency=False);save(output/'report.json',report)
        print(json.dumps({'status':'complete','checks':127,'sanitize':args.sanitize}),flush=True)
    except Exception as error:
        report.update(status='failed',error=str(error));save(output/'report.json',report);raise

if __name__=='__main__':main()

"""Export exact accepted source bytes and verify an independent read-only rebuild."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tarfile
import os

ROOT=Path(__file__).resolve().parents[1]
PROTOCOL=ROOT/'docs/PROTOCOL_BM_MODEL16_BUNDLE_20261030.md'

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    source=ROOT/'builds/bm_model16_snapshot_acceptance_20261030'
    archived=ROOT/'07_Eval_Integration/results/bm_model16_snapshot_acceptance_20261030'
    watched={str(Path(__file__).resolve()):sha(Path(__file__)),str(PROTOCOL):sha(PROTOCOL)}
    report={'status':'running','accepted_revision':'421eb36','watched':watched}
    try:
        for filename in ('report.json','identity.json','members.json'):
            path=archived/filename
            frozen=subprocess.check_output(['git','-C',str(ROOT),'show','421eb36:'+path.relative_to(ROOT).as_posix()])
            if path.read_bytes()!=frozen:raise ValueError('accepted bundle metadata preimage drift')
            watched[str(path)]=sha(path)
        accepted=json.loads((archived/'report.json').read_text())
        if accepted['status']!='complete' or not accepted['functional_space_acceptance']:
            raise ValueError('snapshot factory acceptance required')
        if sha(source/'report.json')!=sha(archived/'report.json'):raise ValueError('accepted build mismatch')
        identity=json.loads((archived/'identity.json').read_text());members=json.loads((archived/'members.json').read_text())
        paths=set()
        for filename in ('contract_opt_deps_0.log','contract_opt_deps_1.log'):
            log=source/filename
            if sha(log)!=members[filename]:raise ValueError('accepted dependency scan drift')
            watched[str(log)]=sha(log)
            for token in log.read_text().replace('\\\n',' ').split()[1:]:paths.add(Path(token).resolve())
        origins={};closure=[]
        for path in sorted(paths):
            if not path.is_relative_to(ROOT) or sha(path)!=identity['closure'][str(path)]:
                raise ValueError('accepted source closure drift: '+str(path))
            watched[str(path)]=sha(path);target=output/path.name
            if target.exists() and target.read_bytes()!=path.read_bytes():raise ValueError('flattened source name collision')
            shutil.copyfile(path,target);closure.append(path.name)
            origins[path.name]={'path':path.relative_to(ROOT).as_posix(),'sha256':sha(path)}
        helper=ROOT/'tools/bm_model16_standalone_rebuild.py';watched[str(helper)]=sha(helper)
        shutil.copyfile(helper,output/'rebuild.py')
        license_path=ROOT/'07_Eval_Integration/upstream_snapshots/benchmark_003a55bf/LICENSE'
        license_bytes=subprocess.check_output(['git','-C',str(license_path.parent),'show',
                                              identity['pins']['benchmark']+':LICENSE'])
        if license_path.read_bytes()!=license_bytes:raise ValueError('benchmark license preimage drift')
        watched[str(license_path)]=sha(license_path)
        (output/'LICENSE.benchmark').write_bytes(license_bytes)
        notices='''Private local research/reproducibility source bundle; no public upload is performed.
Benchmark sources are from the pinned zsh20070718/learned-index repository.
Its GNU GPLv3 license is reproduced as LICENSE.benchmark; source bytes are unchanged.
BlockModel sources are from pinned Zephyr761/Learned-Index-Model. No top-level
license file is present in that pinned model tree; no license is inferred here.
Workspace-authored sources and generated family have individual origins in provenance.json.
No binaries, datasets or Git metadata are part of the exported source manifest.
'''
        (output/'THIRD_PARTY_NOTICES.txt').write_bytes(notices.encode())
        readme='''# Model16 read-only snapshot: private reproducibility bundle

This is the exact source closure of the accepted snapshot factory at421eb36.
Context16 remains production default. Only bulk_load/find/owning scan are supported;
mutations and range_sum throw. Model commit and all source hashes are in manifest.json.
Functionality/space passed; no elapsed-performance or adoption claim is made.

Rebuild from this directory with an x86-64-v3-capable GCC toolchain and Python3.10+:

    python3 -I -B rebuild.py --output fresh_opt
    python3 -I -B rebuild.py --output fresh_san --sanitize

Each output directory must be new. The script needs no Git, network, dataset or
original workspace; Python standard library and the system C++ toolchain are needed.
It scans actual dependencies, rejects source outside this bundle, hashes tools and
sources, atomically publishes the binary and runs the same127-check public contract.
The sanitizer run requires the system GCC ASan/UBSan runtimes. Raw logs and hashes
remain in each fresh output directory, including failures.

This source-only contract rebuild does not repeat the original32/fault/space matrix.
See provenance.json and THIRD_PARTY_NOTICES.txt for original evidence and source origins.
'''
        (output/'README.md').write_bytes(readme.encode())
        save(output/'provenance.json',{'accepted_revision':'421eb36','source_report_sha256':sha(archived/'report.json'),
             'source_identity_sha256':sha(archived/'identity.json'),'origins':origins,'pins':identity['pins'],
             'model_top_level_license':None,'benchmark_license':'LICENSE.benchmark','timing_claims':False})
        files={p.name:sha(p) for p in output.iterdir() if p.is_file()}
        manifest={'files':files,'expected_closure':sorted(set(closure)),
                  'compiled_sources':['contract.cpp','block_model.cpp'],'model_commit':identity['pins']['model'],
                  'accepted_revision':'421eb36','scope':'read-only snapshot, private local reproduction'}
        save(output/'manifest.json',manifest);report['manifest_sha256']=sha(output/'manifest.json')
        report['builds']={};save(output/'export_report.json',report)
        for name,sanitize in (('opt',False),('sanitizer',True)):
            command=[sys.executable,'-I','-B',str(output/'rebuild.py'),'--output',str(output/('_'+name))]
            if sanitize:command.append('--sanitize')
            with (output/(name+'_launcher.log')).open('w') as stream:
                result=subprocess.run(command,cwd=output,stdout=stream,stderr=subprocess.STDOUT)
            if result.returncode:raise RuntimeError(name+' standalone rebuild failed; logs retained')
            rebuilt=json.loads((output/('_'+name)/'report.json').read_text())
            if rebuilt['status']!='complete' or rebuilt['contract_checks']!=127:raise ValueError('clean rebuild contract')
            if rebuilt['source_closure']!={p:files[p] for p in manifest['expected_closure']}:
                raise ValueError('clean rebuilt source bytes differ')
            report['builds'][name]={'report_sha256':sha(output/('_'+name)/'report.json'),
                                   'checks':127,'workspace_dependency':rebuilt['workspace_dependency']}
            save(output/'export_report.json',report);print(name+': standalone127 complete',flush=True)
        if any(sha(p)!=h for p,h in watched.items()):raise ValueError('export input/source drift')
        if sha(output/'manifest.json')!=report['manifest_sha256'] or any(sha(output/name)!=h for name,h in files.items()):
            raise ValueError('exported bundle drift')
        source_members={**files,'manifest.json':report['manifest_sha256']}
        partial=output/'source_bundle.tar.gz.partial';archive_path=output/'source_bundle.tar.gz'
        with tarfile.open(partial,'w:gz') as archive:
            for name in sorted(source_members):archive.add(output/name,arcname=name,recursive=False)
        with tarfile.open(partial,'r:gz') as archive:
            entries=archive.getmembers()
            if len(entries)!=len(source_members) or {m.name for m in entries}!=set(source_members):
                raise ValueError('source archive member coverage')
            for member in entries:
                if not member.isfile() or hashlib.sha256(archive.extractfile(member).read()).hexdigest()!=source_members[member.name]:
                    raise ValueError('source archive member mismatch')
        os.replace(partial,archive_path);save(output/'source_bundle_members.json',source_members)
        report['source_archive']={'name':archive_path.name,'sha256':sha(archive_path),'members':len(source_members),
                                  'binaries_or_datasets':False}
        report.update(status='complete',manifest_files=len(files),compiled_closure_files=len(manifest['expected_closure']));save(output/'export_report.json',report)
        print(json.dumps({'status':'complete','source_files':len(files),'contract_checks_per_build':127}),flush=True)
    except Exception as error:
        report.update(status='failed',error=str(error));save(output/'export_report.json',report);raise

if __name__=='__main__':main()

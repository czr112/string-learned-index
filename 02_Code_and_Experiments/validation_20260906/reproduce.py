"""Rebuild and rerun the delivered experiments on a Linux C++17 host."""
from pathlib import Path
import argparse,hashlib,io,json,os,subprocess,sys,urllib.request,zipfile

root=Path(__file__).resolve().parent
p=argparse.ArgumentParser()
p.add_argument('--full',action='store_true',help='also download pinned Tranco data and run all timed experiments')
args=p.parse_args()

def run(cmd,**kw):
    print('+',' '.join(map(str,cmd)),flush=True)
    return subprocess.run(list(map(str,cmd)),cwd=root,check=True,**kw)

for folder in [root,root/'overflow_probe']:
    run(['g++','-std=c++17','-O1','-g','-march=native','-fsanitize=address,undefined','-fno-omit-frame-pointer',folder/'check.cpp','-o',folder/'check_san'])
    # ASan/UBSan remain active. The original container could not complete LSAN's process scan.
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1'
    with (folder/'correctness.json').open('w') as output:run([folder/'check_san'],env=env,stdout=output)
    run(['g++','-std=c++17','-O3','-DNDEBUG','-march=native','-Wall','-Wextra',folder/'bench.cpp','-o',folder/'bench'])
run(['g++','-std=c++17','-O3','-DNDEBUG','-march=native','-Wall','-Wextra',root/'query_copy_probe'/'bench.cpp','-o',root/'query_copy_probe'/'bench'])
run(['g++','-std=c++17','-O3','-DNDEBUG','-march=native',root/'updates.cpp','-o',root/'updates'])
with (root/'update_events.csv').open('w') as output,(root/'update_events.log').open('w') as errors:
    run(['taskset','-c',str(min(os.sched_getaffinity(0))),root/'updates'],stdout=output,stderr=errors)
if args.full:
    metadata=json.loads((root/'tranco_source.json').read_text());directory=root/'data';directory.mkdir(exist_ok=True)
    csv_file=directory/'tranco_XN6XN.csv'
    if not csv_file.exists():
        with urllib.request.urlopen(metadata['download_url'],timeout=45) as response:payload=response.read()
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:content=archive.read(archive.namelist()[0])
        if hashlib.sha256(content).hexdigest()!=metadata['csv_sha256']:raise RuntimeError('Pinned data hash mismatch')
        csv_file.write_bytes(content)
    if hashlib.sha256(csv_file.read_bytes()).hexdigest()!=metadata['csv_sha256']:raise RuntimeError('Local data hash mismatch')
    for suite in ['main','diagnostic']:
        run([sys.executable,root/'run_experiments.py','--suite',suite,'--tranco',csv_file])
    run([sys.executable,root/'run_overflow_probe.py',csv_file])
    run([sys.executable,root/'run_query_copy_probe.py',csv_file])
    run([sys.executable,root/'analyze.py'])
print('Completed. Rerunning in this directory replaces local result files; keep an untouched copy of the delivered archive.')

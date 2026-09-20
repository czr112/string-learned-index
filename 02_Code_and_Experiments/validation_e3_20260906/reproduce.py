"""Compile, check, and optionally rerun the E3 protocol on a Linux host."""
from pathlib import Path
import argparse,hashlib,io,json,os,subprocess,sys,urllib.request,zipfile

root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--full',action='store_true');p.add_argument('--tranco',type=Path)
args=p.parse_args()
def run(command,**kw):
    print('+',' '.join(map(str,command)),flush=True)
    subprocess.run(list(map(str,command)),cwd=root,check=True,**kw)

run(['g++','-std=c++17','-O1','-g','-march=native','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wall','-Wextra',root/'check.cpp','-o',root/'check_san'])
env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1'
with (root/'correctness.json').open('w') as log:run([root/'check_san'],env=env,stdout=log)
run(['g++','-std=c++17','-O3','-DNDEBUG','-march=native','-Wall','-Wextra',root/'bench.cpp','-o',root/'bench'])
if args.full:
    meta=json.loads((root/'tranco_source.json').read_text())
    path=args.tranco
    if path is None:
        directory=root/'data';directory.mkdir(exist_ok=True);path=directory/'tranco_XN6XN.csv'
        if not path.exists():
            with urllib.request.urlopen(meta['download_url'],timeout=45) as response:payload=response.read()
            with zipfile.ZipFile(io.BytesIO(payload)) as z:content=z.read(z.namelist()[0])
            if hashlib.sha256(content).hexdigest()!=meta['csv_sha256']:raise RuntimeError('Pinned data hash mismatch')
            path.write_bytes(content)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=meta['csv_sha256']:raise RuntimeError('Pinned data hash mismatch')
    run([sys.executable,root/'run.py','--suite','main','--tranco',path.resolve()])
    run([sys.executable,root/'analyze.py'])
print('Completed. Run on a copy to preserve the delivered raw results.')

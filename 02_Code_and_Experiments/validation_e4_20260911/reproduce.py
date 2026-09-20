"""Build/check E4. --full repeats the fixed main protocol into a new directory."""
from pathlib import Path
import argparse,hashlib,io,json,os,subprocess,sys,urllib.request,zipfile
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--full',action='store_true');p.add_argument('--tranco',type=Path);p.add_argument('--output',type=Path,default=root/'results/reproduced');a=p.parse_args()
def run(cmd,**kw):
    print('+',' '.join(map(str,cmd)),flush=True);return subprocess.run(list(map(str,cmd)),cwd=root,check=True,**kw)
run([sys.executable,root/'fetch_sources.py']);run([sys.executable,root/'apply_lits_patch.py']);(root/'build').mkdir(exist_ok=True)
run(['gcc','-std=c99','-O3','-march=native','-c',root/'vendor/libart/src/art.c','-o',root/'build/art.o'])
for source in ['check_native','bench']:
    run(['g++','-std=c++17','-O3','-march=native','-w',root/f'{source}.cpp',root/'build/art.o','-o',root/f'build/{source}'])
run([root/'build/check_native','art'])
# LITS failed the sanitizer gate and is excluded from the official benchmark.
# check_native lits remains available for diagnostic work; see NATIVE_STATUS.md.
run(['g++','-std=c++17','-O1','-g','-march=native','-fsanitize=address,undefined','-fno-omit-frame-pointer',root/'check_selector.cpp','-o',root/'build/check_selector_san'])
env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1'
run([root/'build/check_selector_san'],env=env)
run(['gcc','-std=c99','-O1','-g','-march=native','-fsanitize=address,undefined','-fno-omit-frame-pointer','-c',root/'vendor/libart/src/art.c','-o',root/'build/art_san.o'])
run(['g++','-std=c++17','-O1','-g','-march=native','-w','-DE4_ART_ONLY','-fsanitize=address,undefined','-fno-omit-frame-pointer',root/'check_native.cpp',root/'build/art_san.o','-o',root/'build/check_art_san'])
run([root/'build/check_art_san'],env=env)
if a.full:
    path=a.tranco
    if path is None:
        path=root/'data/tranco_XN6XN.csv';path.parent.mkdir(exist_ok=True)
        if not path.exists():
            with urllib.request.urlopen('https://tranco-list.eu/download_daily/XN6XN',timeout=60) as r:payload=r.read()
            with zipfile.ZipFile(io.BytesIO(payload)) as z:payload=z.read(z.namelist()[0])
            if hashlib.sha256(payload).hexdigest()!='8ab2bff1614420a4adafcf29963c5c31363c22026454161905a43a12ba74243e':raise RuntimeError('Tranco hash mismatch')
            path.write_bytes(payload)
    run([sys.executable,root/'run.py','--tranco',path.resolve(),'--output',a.output.resolve()])
print('Completed. Raw delivered results are not overwritten.')

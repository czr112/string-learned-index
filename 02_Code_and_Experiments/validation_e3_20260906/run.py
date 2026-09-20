"""Sequential pinned runs; all parameters fixed in PROTOCOL.md."""
from pathlib import Path
import argparse,csv,hashlib,json,os,statistics,subprocess,time

root=Path(__file__).resolve().parent
p=argparse.ArgumentParser()
p.add_argument('--suite',choices=['pilot','main'],default='main')
p.add_argument('--tranco',type=Path,default=root.parent/'research_cache/tranco_XN6XN.csv')
p.add_argument('--case',choices=['skew','mixed','tranco'])
args=p.parse_args()
cases=[('mixed',8192,65536,2,1)] if args.suite=='pilot' else [('skew',131072,262144,5,3),('mixed',262144,262144,5,3),('tranco',1000000,524288,5,3)]
if args.case:cases=[c for c in cases if c[0]==args.case]
if args.suite=='main':
    expected=json.loads((root/'tranco_source.json').read_text())['csv_sha256']
    if hashlib.sha256(args.tranco.read_bytes()).hexdigest()!=expected:raise RuntimeError('Pinned Tranco data mismatch')
for kind,n,q,rounds,seeds in cases:
    directory=root/'results'/args.suite/f'{kind}_{n}';directory.mkdir(parents=True,exist_ok=True)
    cmd=['taskset','-c',str(min(os.sched_getaffinity(0))),str(root/'bench'),kind,str(n),str(directory),str(args.tranco),str(q),str(rounds),str(seeds)]
    metadata={'command':cmd,'queries':q,'rounds_per_seed':rounds,'query_seeds':seeds,'suite':args.suite,'source_sha256':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ['index.hpp','selected.hpp','data.hpp','bench.cpp','PROTOCOL.md']}}
    (directory/'run.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('START',kind,n,flush=True);t0=time.monotonic()
    with (directory/'run.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=log,check=True)
    rows=list(csv.DictReader((directory/'timings.csv').open()));groups={}
    for name in ['candidates.csv','selections.csv','joint_groups.csv','timings.csv']:
        path=directory/name;rr=list(csv.DictReader(path.open()))
        if not path.read_bytes().endswith(b'\n') or any(any(v is None for v in r.values()) for r in rr):raise RuntimeError(f'Truncated file: {path}')
        expected={'candidates.csv':((n+2047)//2048)*10,'selections.csv':((n+2047)//2048)*6,'timings.csv':rounds*seeds*7}
        if name in expected and len(rr)!=expected[name]:raise RuntimeError(f'Incomplete file: {path}')
    for r in rows:groups.setdefault(r['method'],[]).append(float(r['ns_per_op']))
    if len(groups)!=7 or any(len(v)!=rounds*seeds for v in groups.values()):raise RuntimeError('Incomplete timings')
    print('DONE',kind,n,'seconds',round(time.monotonic()-t0,1),json.dumps({k:round(statistics.median(v),2) for k,v in groups.items()},sort_keys=True),flush=True)

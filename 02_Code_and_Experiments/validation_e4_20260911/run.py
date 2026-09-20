"""Fixed sequential E4 runs; refuses to overwrite an existing case directory."""
from pathlib import Path
import argparse,csv,hashlib,json,os,statistics,subprocess,time
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--tranco',type=Path,default=root.parent/'research_cache/tranco_XN6XN.csv');p.add_argument('--case',choices=['skew','mixed','tranco']);p.add_argument('--output',type=Path,default=root/'results/main');a=p.parse_args()
if not a.case or a.case=='tranco':
    if hashlib.sha256(a.tranco.read_bytes()).hexdigest()!='8ab2bff1614420a4adafcf29963c5c31363c22026454161905a43a12ba74243e':raise RuntimeError('Tranco hash mismatch')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
for kind,n in [('skew',131072),('mixed',262144),('tranco',1000000)]:
    if a.case and a.case!=kind:continue
    d=a.output/f'{kind}_{n}';d.mkdir(parents=True,exist_ok=False)
    cmd=['taskset','-c',str(min(os.sched_getaffinity(0))),str(root/'build/bench'),kind,str(n),str(d),str(a.tranco),'262144','5','3']
    source_paths=[root/x for x in ['bench.cpp','selector.hpp','native.hpp','PROTOCOL.md','sources.json']]+[root.parent/'validation_e3_20260906'/x for x in ['index.hpp','selected.hpp','data.hpp']]
    meta={'command':cmd,'date_utc':'2026-09-11','source_sha256':{str(x.relative_to(root.parent)):sha(x) for x in source_paths},'binary_sha256':sha(root/'build/bench'),'protocol_sha256':sha(root/'PROTOCOL.md')}
    (d/'run.json').write_text(json.dumps(meta,indent=2)+'\n');t=time.monotonic();print('START',kind,n,flush=True)
    with (d/'run.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=log,check=True)
    done=json.loads((d/'complete.json').read_text());frozen=json.loads((d/'frozen.json').read_text())
    for name,count in [('candidates.csv',((n+2047)//2048)*10),('validation.csv',len(frozen['policies'])*5),('timings.csv',done['rows'])]:
        path=d/name;rows=list(csv.DictReader(path.open()));
        if not path.read_bytes().endswith(b'\n') or len(rows)!=count or any(None in r or any(v is None for v in r.values()) for r in rows):raise RuntimeError(f'incomplete CSV {path}')
    groups={}
    for r in rows:groups.setdefault(r['method'],[]).append(float(r['ns_per_op']))
    if len(groups)!=done['methods'] or any(len(v)!=15 for v in groups.values()):raise RuntimeError('incomplete timings')
    print('DONE',kind,'seconds',round(time.monotonic()-t,1),'winner',frozen['validation_winner'],json.dumps({k:round(statistics.median(v),2) for k,v in groups.items()},sort_keys=True),flush=True)

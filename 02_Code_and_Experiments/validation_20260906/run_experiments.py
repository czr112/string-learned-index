"""Sequential, pinned-CPU experiments. Raw rows are never replaced by averages."""
from pathlib import Path
import argparse, csv, json, os, statistics, subprocess, time

ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser()
p.add_argument('--suite',choices=['pilot','main','diagnostic'],default='main')
p.add_argument('--tranco',type=Path,default=ROOT.parent/'research_cache/tranco_XN6XN.csv')
p.add_argument('--cpu',type=int,default=min(os.sched_getaffinity(0)))
args=p.parse_args()
if args.suite=='pilot':
    cases=[('short',32768,512,200000,5,2048,'end'),('long',131072,1024,262144,5,2048,'end')]
elif args.suite=='diagnostic':
    cases=[('skew',131072,1024,262144,7,256,'end'),('tranco',1000000,512,1000000,7,512,'end')]
else:
    cases=[
        ('long',32768,1024,200000,7,2048,'end'),
        ('short',262144,512,524288,7,2048,'end'),
        ('long',262144,512,524288,7,2048,'end'),
        ('long',262144,1024,524288,7,2048,'end'),
        ('long',262144,2048,524288,7,2048,'end'),
        ('skew',131072,1024,262144,7,2048,'end'),
        ('long',1048576,1024,1048576,7,2048,'end'),
        ('tranco',1000000,512,1000000,7,2048,'end'),
        ('long',262144,1024,524288,7,2048,'local'),
    ]
outdir=ROOT/'results'/args.suite
outdir.mkdir(parents=True,exist_ok=True)
for kind,n,b,q,reps,regions,scope in cases:
    name=f'{kind}_{n}_b{b}_{scope}'
    if args.suite=='diagnostic':name+=f'_r{regions}'
    cmd=['taskset','-c',str(args.cpu),str(ROOT/'bench'),kind,str(n),str(b),str(q),str(reps),str(regions),scope,str(args.tranco),'10']
    print('START',name,flush=True)
    before=time.monotonic()
    with (outdir/(name+'.csv')).open('w') as stdout,(outdir/(name+'.log')).open('w') as stderr:
        subprocess.run(cmd,stdout=stdout,stderr=stderr,check=True)
    rows=list(csv.DictReader((outdir/(name+'.csv')).open()))
    grouped={}
    for row in rows:grouped.setdefault(row['mode'],[]).append(float(row['ns_per_op']))
    med={k:round(statistics.median(v),2) for k,v in grouped.items()}
    print('DONE',name,'seconds',round(time.monotonic()-before,1),'median_ns',json.dumps(med,sort_keys=True),flush=True)
print('ALL_COMPLETED',args.suite,flush=True)

from pathlib import Path
import csv,json,os,statistics,subprocess,sys
root=Path(__file__).resolve().parent
out=root/'overflow_probe'/'results';out.mkdir(exist_ok=True)
source=Path(sys.argv[1]) if len(sys.argv)>1 else root.parent/'research_cache/tranco_XN6XN.csv'
for kind,n,b,q in [('skew',131072,1024,262144),('tranco',1000000,512,1000000)]:
    name=f'{kind}_{n}_b{b}_r2048'
    cmd=['taskset','-c',str(min(os.sched_getaffinity(0))),str(root/'overflow_probe'/'bench'),kind,str(n),str(b),str(q),'7','2048','end',str(source),'10']
    print('START',name,flush=True)
    with (out/(name+'.csv')).open('w') as f,(out/(name+'.log')).open('w') as e:subprocess.run(cmd,stdout=f,stderr=e,check=True)
    rows=list(csv.DictReader((out/(name+'.csv')).open()));groups={}
    for r in rows:groups.setdefault(r['mode'],[]).append(float(r['ns_per_op']))
    if any(len(v)!=7 for v in groups.values()) or len(groups)!=3:raise RuntimeError('Incomplete overflow probe')
    print('DONE',name,json.dumps({k:statistics.median(v) for k,v in groups.items()}),flush=True)

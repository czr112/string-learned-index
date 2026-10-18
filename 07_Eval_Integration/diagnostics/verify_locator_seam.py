"""Independent reconstruction of B-stage source identities, gates and paired decisions."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import statistics
import sys

WORKSPACE=Path(__file__).resolve().parents[2]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    out=Path(sys.argv[1]).resolve();meta=json.loads((out/'identity.json').read_text())
    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source',path)
    for name,h in meta['binaries'].items():assert sha(out/'build'/name)==h,('binary',name)
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries_sha256']
    gates=json.loads((out/'gates.json').read_text());assert len(gates)==4
    for g in gates:
        assert g['normal']==g['sanitized'] and g['sanitizer_rc']==g['sanitizer_stderr_bytes']==0
        assert g['normal']['work_checks']==300000 and g['normal']['queries']==100000
        assert g['normal']['rows']==meta['inputs'][g['cell']]['rows']
    raw=json.loads((out/'raw.json').read_text());summary=json.loads((out/'summary.json').read_text())
    assert len(raw)==540 and len(summary)==3;count=0
    for cell in summary:
        rows=[r for r in raw if r['cell']==cell['cell']];assert len(rows)==180
        arms={r['arm'] for r in rows};assert len(arms)==6 and len({r['checksum'] for r in rows})==1
        assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in rows)
        for process in range(5):
            sample=[r for r in rows if r['process']==process];assert len(sample)==36;pairs=collections.Counter()
            for rep in range(6):
                seq=sorted((r for r in sample if r['repeat']==rep),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==arms
                for x,y in zip(seq,seq[1:]):pairs[x['arm'],y['arm']]+=1
            assert len(pairs)==30 and set(pairs.values())=={1}
            for a in arms:assert sorted(r['position'] for r in sample if r['arm']==a)==list(range(6))
        for name,r in cell['ratios'].items():
            a,b=name.split('/');medians=[]
            for process in range(5):
                times={(x['repeat'],x['arm']):x['elapsed_ns'] for x in rows if x['process']==process}
                medians.append(statistics.median(times[rep,a]/times[rep,b] for rep in range(6)));count+=6
            assert medians==r['process_medians'] and statistics.median(medians)==r['median']
            assert r['faster']==sum(x<1 for x in medians) and r['slower']==sum(x>1 for x in medians)
        for policy in ['model','directory']:
            f=max(max(x,1/x) for x in cell['ratios']['new_'+policy+'_AA/new_'+policy]['process_medians'])
            assert f==cell['aa_observed_bands'][policy]
            for name,r in cell['ratios'].items():
                if ('directory' in name)!=(policy=='directory'):continue
                assert r['clear_slowdown']==(r['median']>1.05 and r['slower']>=4 and r['median']>f)
                assert r['clear_gain']==(r['median']<.95 and r['faster']>=4 and 1/r['median']>f)
    archive=json.loads((out/'log_archive.json').read_text());assert sha(out/'commands.log.gz')==archive['gzip_sha256']
    assert hashlib.sha256(gzip.decompress((out/'commands.log.gz').read_bytes())).hexdigest()==archive['raw_sha256']
    result={'verified':True,'gates':4,'timing_rows':len(raw),'paired_ratios':count,'sources':len(meta['sources'])}
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()

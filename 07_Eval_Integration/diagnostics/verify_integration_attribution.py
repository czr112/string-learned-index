"""Independent A-stage source/input, order, checksum and paired-ratio audit."""
import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser();p.add_argument('results');a=p.parse_args();out=Path(a.results).resolve()
    meta=json.loads((out/'identity.json').read_text());raw=json.loads((out/'raw.json').read_text())
    summaries=json.loads((out/'summary.json').read_text());assert len(summaries)==3 and len(raw)==540
    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source identity',path)
    for corpus,item in meta['inputs'].items():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries_sha256']
    assert sha(out.parent/'build/bench')==meta['binary_sha256']
    count=0
    for cell in summaries:
        rows=[r for r in raw if r['corpus']==cell['corpus']];assert len(rows)==180
        arms={r['arm'] for r in rows};assert len(arms)==6 and len({r['checksum'] for r in rows})==1
        for process in range(5):
            sample=[r for r in rows if r['process']==process];assert len(sample)==36
            pairs=collections.Counter()
            for repeat in range(6):
                seq=sorted([r for r in sample if r['repeat']==repeat],key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==arms
                for x,y in zip(seq,seq[1:]):pairs[x['arm'],y['arm']]+=1
            assert len(pairs)==30 and set(pairs.values())=={1}
            for arm in arms:assert sorted(r['position'] for r in sample if r['arm']==arm)==list(range(6))
        for comparison,reported in cell['ratios'].items():
            x,y=comparison.split('/');medians=[]
            for process in range(5):
                values={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                medians.append(statistics.median(values[rep,x]/values[rep,y] for rep in range(6)));count+=6
            assert medians==reported['process_medians'] and statistics.median(medians)==reported['median']
        aa=cell['ratios']['new_AA/new_checked']['process_medians'];band=max(max(x,1/x) for x in aa)
        assert band==cell['aa_observed_band']
        for ratio in cell['ratios'].values():
            assert ratio['slower']==sum(x>1 for x in ratio['process_medians'])
            assert ratio['faster']==sum(x<1 for x in ratio['process_medians'])
            assert ratio['clear_slowdown']==(ratio['median']>1.05 and ratio['slower']>=4 and ratio['median']>band)
            assert ratio['clear_gain']==(ratio['median']<.95 and ratio['faster']>=4 and 1/ratio['median']>band)
    gates=json.loads((out/'gates.json').read_text());assert len(gates)==3
    assert all(g['gate']['core_memory_equal'] and g['work']['crc_differences']==0 for g in gates)
    archive=json.loads((out/'log_archive.json').read_text());assert sha(out/'commands.log.gz')==archive['gzip_sha256']
    assert hashlib.sha256(gzip.decompress((out/'commands.log.gz').read_bytes())).hexdigest()==archive['raw_sha256']
    result={'verified':True,'cells':3,'timing_rows':len(raw),'paired_ratios':count,'source_files':len(meta['sources'])}
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()

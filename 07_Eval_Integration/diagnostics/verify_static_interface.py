"""Independent audit of source/input identities, six-arm order, checksums and ratios."""
import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE=Path(__file__).resolve().parents[2]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('results');args=parser.parse_args();out=Path(args.results).resolve()
    meta=json.loads((out/'identity.json').read_text());raw=json.loads((out/'raw.json').read_text());summary=json.loads((out/'summary.json').read_text())
    for path,sha in meta['sources'].items():assert digest(WORKSPACE/path)==sha,('source identity',path)
    for path,sha in meta['reused_objects'].items():assert digest(WORKSPACE/path)==sha,('object identity',path)
    for cell,item in meta['inputs'].items():
        assert digest(WORKSPACE/item['rows_path'])==item['rows_sha256'];assert digest(WORKSPACE/item['queries_path'])==item['queries']['sha256']
    checked_pairs=0
    for cell in summary:
        rows=[r for r in raw if r['cell']==cell['cell'] and r['phase']=='paired'];assert len(rows)==180
        arms=set(r['arm'] for r in rows);assert len(arms)==6
        for process in range(5):
            sample=[r for r in rows if r['process']==process];assert len(sample)==36 and len({r['checksum'] for r in sample})==1
            assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in sample)
            predecessor=collections.Counter()
            for repeat in range(6):
                seq=sorted([r for r in sample if r['repeat']==repeat],key=lambda r:r['position']);assert len(seq)==6 and {r['arm'] for r in seq}==arms
                assert [r['position'] for r in seq]==list(range(6))
                for a,b in zip(seq,seq[1:]):predecessor[a['arm'],b['arm']]+=1
            assert len(predecessor)==30 and set(predecessor.values())=={1}
            for arm in arms:assert sorted(r['position'] for r in sample if r['arm']==arm)==list(range(6))
        for comparison,reported in cell['ratios'].items():
            a,b=comparison.split('/');medians=[]
            for process in range(5):
                values={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                medians.append(statistics.median(values[rep,a]/values[rep,b] for rep in range(6)));checked_pairs+=6
            assert medians==reported['process_medians'] and statistics.median(medians)==reported['median']
            assert sum(r<1 for r in medians)==reported['faster'] and sum(r>1 for r in medians)==reported['slower']
        aa=max(max(r,1/r) for r in cell['ratios']['learned_AA/learned']['process_medians']);assert aa==cell['aa_observed_band']
        integration=cell['ratios']['learned/prototype'];candidate=cell['ratios']['wide_directory/learned']
        assert cell['integration_regression']==(integration['median']>1.05 and integration['slower']>=4 and integration['median']>aa)
        assert cell['candidate_gain']==(candidate['median']<=.90 and candidate['faster']>=4 and 1/candidate['median']>aa)
        assert cell['candidate_regression']==(candidate['median']>1.05 and candidate['slower']>=4 and candidate['median']>aa)
    gates=json.loads((out/'gates.json').read_text());assert len(gates)==4
    assert all(g['sanitizer_rc']==0 and g['sanitizer_stderr_bytes']==0 and g['checks']>0 and g['scans']>0 for g in gates)
    decision=json.loads((out/'decision.json').read_text())
    assert decision['integration_regression']==any(c['integration_regression'] for c in summary)
    assert decision['skew_gain_pass']==all(c['candidate_gain'] for c in summary if c['cell'].startswith('skew_'))
    assert decision['risk_regression']==any(c['candidate_regression'] for c in summary if not c['cell'].startswith('skew_'))
    assert decision['retain_wide_directory']==(not decision['integration_regression'] and decision['skew_gain_pass'] and not decision['risk_regression'])
    archive=json.loads((out/'log_archive.json').read_text());assert digest(out/'commands.log.gz')==archive['archive_sha256']
    original=gzip.decompress((out/'commands.log.gz').read_bytes());assert hashlib.sha256(original).hexdigest()==archive['original_sha256'] and len(original)==archive['original_bytes']
    result={'cells':len(summary),'timing_rows':sum(r['phase']=='paired' for r in raw),'ratio_pairs':checked_pairs,'source_files':len(meta['sources']),'verified':True}
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()

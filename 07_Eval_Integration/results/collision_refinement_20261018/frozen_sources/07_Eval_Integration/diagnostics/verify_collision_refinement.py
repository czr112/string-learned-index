"""Independent D1 entrance and paired-matrix reconstruction, without importing runners."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import statistics
import struct
import sys

WORKSPACE=Path(__file__).resolve().parents[2]


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def identities(out):
    meta=read(out/'identity.json')
    for path,h in meta['sources'].items():assert sha(WORKSPACE/path)==h,('source',path)
    for name,h in meta['binaries'].items():assert sha(out/'build'/name)==h,('binary',name)
    for item in meta['inputs'].values():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries_sha256']
    archive=read(out/'log_archive.json');assert sha(out/'commands.log.gz')==archive['gzip_sha256']
    data=gzip.decompress((out/'commands.log.gz').read_bytes());assert hashlib.sha256(data).hexdigest()==archive['raw_sha256']
    return meta,[json.loads(line) for line in data.decode().splitlines() if line.startswith('{')]


def entry(out):
    meta,emitted=identities(out);rows=read(out/'results.json');assert len(rows)==8 and len(emitted)==10
    domain=read(out/'domain.json');assert domain['normal']==domain['sanitized'] and domain['rc']==domain['stderr_bytes']==0
    assert domain['normal']['domain_checks']==979091 and emitted[:2]==[domain['normal'],domain['sanitized']]
    assert emitted[2:]==[r['data'] for r in rows]
    main={'skew_full_10000','skew_full_100000'}
    for r in rows:
        d=r['data'];n=d['queries'];assert n==int(r['cell'].rsplit('_',1)[1]) and r['main']==(r['cell'] in main)
        with (WORKSPACE/meta['inputs'][r['cell']]['rows_path']).open('rb') as f:header=f.read(16)
        assert header[:8]==b'SIBROW1\0' and struct.unpack_from('<Q',header,8)[0]==d['rows']
        gates={'coverage':d['covered']/n>=.8,'narrow_coverage':d['covered']>0 and d['local_narrow']/d['covered']>=.8,
            'entered_blocks':d['new_blocks']<=.5*d['old_blocks'],'space_budget':d['extra_bytes']<=.05*d['core_bytes']}
        assert gates==r['entry_tests'] and all(gates.values())==r['passed']
    decision=read(out/'decision.json')
    passed=all(r['passed'] for r in rows if r['main']) and all(r['entry_tests']['space_budget'] for r in rows)
    assert decision['entry_passed']==passed and decision['failed_main_cells']==[r['cell'] for r in rows if r['main'] and not r['passed']]
    assert decision['main_cells']==sorted(main) and not decision['timing_performed'] and not decision['production_changed']
    result={'verified':True,'cells':8,'queries':sum(r['data']['queries'] for r in rows),'entry_passed':passed,
        'verification_source_sha256':sha(Path(__file__))}
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    return {r['cell']:r['data'] for r in rows},result


def matrix(out):
    meta,emitted=identities(out)
    for p,h in meta['entry_evidence'].items():assert sha(WORKSPACE/p)==h,('entry evidence',p)
    edata,_=entry(WORKSPACE/'07_Eval_Integration/results/collision_refinement_entry_20261018')
    gates=read(out/'gates.json');assert len(gates)==4
    for g in gates:
        d=g['normal'];assert d==g['sanitized'] and g['sanitizer_rc']==g['sanitizer_stderr_bytes']==0
        assert d['work_checks']==d['queries']==100000
        for name in ['old_blocks','new_blocks','covered','local_narrow','parent_corrections','family_steps','local_steps','prefix_bytes','directory_steps']:
            assert d[name]==edata[g['cell']][name],('entry versus actual',g['cell'],name)
        assert d['refinement_bytes']==edata[g['cell']]['extra_bytes'] and d['core_bytes']==edata[g['cell']]['core_bytes']+d['refinement_bytes']
    raw=read(out/'raw.json');summaries=read(out/'summary.json');assert len(raw)==1440 and len(summaries)==8
    arms={'B_learned','B_AA','B_wide','B_directory','D1','D1_AA'};pair_names={'D1/B_learned','D1/B_wide','D1/B_directory','B_AA/B_learned','D1_AA/D1'}
    emitted_pairs=[r for r in emitted if r.get('phase')=='paired'];assert len(emitted_pairs)==1440
    assert [{k:v for k,v in r.items() if k not in ['cell','process']} for r in raw]==emitted_pairs
    count=0
    for s in summaries:
        rows=[r for r in raw if r['cell']==s['cell']];assert len(rows)==180 and len({r['checksum'] for r in rows})==1
        assert set(s['ratios'])==pair_names and {r['arm'] for r in rows}==arms
        assert all(r['operations']==200000 and r['elapsed_ns']>0 for r in rows)
        for p in range(5):
            sample=[r for r in rows if r['process']==p];assert len(sample)==36;pairs=collections.Counter()
            for rep in range(6):
                seq=sorted((r for r in sample if r['repeat']==rep),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(6)) and {r['arm'] for r in seq}==arms
                for a,b in zip(seq,seq[1:]):pairs[a['arm'],b['arm']]+=1
            assert len(pairs)==30 and set(pairs.values())=={1}
            for a in arms:assert sorted(r['position'] for r in sample if r['arm']==a)==list(range(6))
        for name,r in s['ratios'].items():
            a,b=name.split('/');medians=[]
            for p in range(5):
                times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==p}
                medians.append(statistics.median(times[rep,a]/times[rep,b] for rep in range(6)));count+=6
            assert medians==r['process_medians'] and statistics.median(medians)==r['median']
            assert r['faster']==sum(x<1 for x in medians) and r['slower']==sum(x>1 for x in medians)
        bands={name:max(max(x,1/x) for x in s['ratios'][name]['process_medians']) for name in ['B_AA/B_learned','D1_AA/D1']}
        f=max(bands.values());assert s['aa_bands']==bands and s['observed_band']==f
        for r in s['ratios'].values():
            assert r['clear_gain']==(r['median']<=.90 and r['faster']>=4 and 1/r['median']>f)
            assert r['clear_regression']==(r['median']>1.05 and r['slower']>=4 and r['median']>f)
        assert s['target_gain']==all(s['ratios']['D1/'+a]['clear_gain'] for a in ['B_learned','B_wide'])
        assert s['risk_regression']==any(s['ratios']['D1/'+a]['clear_regression'] for a in ['B_learned','B_wide'])
    target=all(s['target_gain'] for s in summaries if s['cell'].startswith('skew_'))
    risks=[s['cell'] for s in summaries if not s['cell'].startswith('skew_') and s['risk_regression']]
    decision=read(out/'decision.json');assert decision['target_gain']==target and decision['risk_regressions']==risks
    assert decision['matrix_eligible']==(target and not risks) and not decision['production_adopted']
    assert decision['original_S3_failure_preserved'] and not decision['ART_HOT_retested'] and decision['external_validation_pending']
    result={'verified':True,'cells':8,'gates':4,'timing_rows':len(raw),'paired_ratios':count,'sources':len(meta['sources'])}
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');return result


if __name__=='__main__':
    out=Path(sys.argv[1]).resolve()
    print(json.dumps(matrix(out) if (out/'raw.json').exists() else entry(out)[1]))

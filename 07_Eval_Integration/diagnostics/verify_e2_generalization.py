"""Independent E2 re-check: rebuild every ratio from raw.json and re-read the gates and ledger."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
ARMS=['prototype','prototype_AA','integrated','integrated_AA','ART','HOT']
PAIRS=[('integrated','prototype'),('integrated','ART'),('integrated','HOT'),('prototype','ART'),
       ('prototype','HOT'),('prototype_AA','prototype'),('integrated_AA','integrated')]
MODES=['warm','first','evicted']


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())


def close(a,b,tol=1e-12):return abs(a-b)<=tol*max(1.0,abs(a),abs(b))


def main():
    p=argparse.ArgumentParser();p.add_argument('out');a=p.parse_args()
    out=Path(a.out).resolve()
    identity=read(out/'identity.json');summary=read(out/'summary.json')
    raw=read(out/'raw.json');gates=read(out/'gates.json');heads=read(out/'process_gates.json')
    ledger=read(out/'ledger.json');decision=read(out/'decision.json')
    notes=[]

    # 1. Recorded sources, artifacts and inputs must still be the bytes on disk.
    for path,h in identity['sources'].items():assert sha(WORKSPACE/path)==h,('source drift',path)
    for path,h in identity['artifacts'].items():assert sha(out/path)==h,('artifact drift',path)
    for cell,item in identity['inputs'].items():
        assert sha(WORKSPACE/item['queries_path'])==item['queries']['sha256']
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert item['queries']['count']==item['rows']
        assert item['queries']['hit']+item['queries']['miss']==item['queries']['count']
        assert (item['queries']['hit']==item['queries']['count'])!=(item['queries']['miss']==item['queries']['count'])
        notes.append(('workload',cell,item['queries']['kind'],item['queries']['count']))

    # 2. Structure and pairing of every timing row.
    assert len(raw)==len(identity['inputs'])*len(MODES)*5*36
    for cell,item in identity['inputs'].items():
        for mode in MODES:
            rows=[r for r in raw if r['cell']==cell and r['mode']==mode]
            assert len(rows)==180
            assert all(r['operations']==item['queries']['count'] and r['elapsed_ns']>0 for r in rows)
            for process in range(5):
                block=[r for r in rows if r['process']==process]
                assert len(block)==36
                for repeat in range(6):
                    seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                    assert [r['position'] for r in seq]==list(range(6))
                    assert {r['arm'] for r in seq}==set(ARMS)
                    # Same key order for every arm in a repeat: the checksum must agree.
                    assert len({r['checksum'] for r in seq})==1,('arm checksum mismatch',cell,mode,process,repeat)
                    if mode=='warm':
                        assert all(r['warm_checksum']==r['checksum'] and r['warm_ns']>0 for r in seq)
                    if mode=='evicted':
                        assert all(r['evict_bytes']==256<<20 for r in seq)
                    if mode=='first':
                        assert all(r['build_ns']>0 for r in seq)
                pairs=collections.Counter()
                for repeat in range(6):
                    seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                    for x,y in zip(seq,seq[1:]):pairs[x['arm'],y['arm']]+=1
                assert len(pairs)==30 and set(pairs.values())=={1}
                for arm in ARMS:assert sorted(r['position'] for r in block if r['arm']==arm)==list(range(6))

    # 3. Rebuild every ratio and every clear-difference flag from the raw rows.
    for recorded in summary:
        cell,mode=recorded['cell'],recorded['mode']
        rows=[r for r in raw if r['cell']==cell and r['mode']==mode]
        ratios={}
        for x,y in PAIRS:
            medians=[]
            for process in range(5):
                times={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                medians.append(statistics.median(times[rep,x]/times[rep,y] for rep in range(6)))
            ratios[x+'/'+y]={'process_medians':medians,'median':statistics.median(medians),
                'faster':sum(v<1 for v in medians),'slower':sum(v>1 for v in medians)}
        bands={name:max(max(v,1/v) for v in ratios[name]['process_medians'])
               for name in ['prototype_AA/prototype','integrated_AA/integrated']}
        f=max(bands.values())
        for name,v in ratios.items():
            v['clear_gain']=v['median']<=.95 and v['faster']>=4 and 1/v['median']>f
            v['clear_regression']=v['median']>1.05 and v['slower']>=4 and v['median']>f
            got=recorded['ratios'][name]
            assert close(got['median'],v['median']) and got['faster']==v['faster'] and got['slower']==v['slower']
            assert got['clear_gain']==v['clear_gain'] and got['clear_regression']==v['clear_regression']
            assert all(close(x,y) for x,y in zip(got['process_medians'],v['process_medians']))
        assert close(recorded['observed_band'],f)
        for arm,value in recorded['ns_per_op'].items():
            assert close(value,statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm))

    # 4. Correctness gates and per-process identity lines.
    assert len(gates)==len(identity['inputs'])
    for gate in gates:
        normal,sanitized=gate['normal'],gate['sanitized']
        assert gate['sanitizer_rc']==0 and gate['sanitizer_stderr_bytes']==0
        for field in ['checks','probes','scans','rejections','work_checks','rows','queries','core_bytes','refinement_bytes']:
            assert normal[field]==sanitized[field],('gate field differs',gate['cell'],field)
        assert normal['queries']==identity['inputs'][gate['cell']]['queries']['count']
        assert normal['work_checks']==normal['queries']
        assert normal['refinement_bytes']<=.05*(normal['core_bytes']-normal['refinement_bytes'])
    assert len(heads)==len(identity['inputs'])*5*6
    for cell in identity['inputs']:
        for process in range(5):
            block=[h for h in heads if h['cell']==cell and h['process']==process]
            assert [h['arm'] for h in block]==ARMS and [h['object_id'] for h in block]==[0,0,1,1,2,3]
            assert all(h['verified']==identity['inputs'][cell]['rows']+identity['inputs'][cell]['queries']['count']
                       for h in block)
            assert block[0]['index_bytes']==block[2]['index_bytes']
            for h in block:assert h['retained_input_bytes']==0

    # 5. Ledger internal consistency.
    assert len(ledger)==4*4
    for record in ledger:
        assert record['wrap_live_bytes']<=record['wrap_peak_live_bytes']<=record['wrap_usable_total_bytes']
        assert record['wrap_frees']<=record['wrap_allocations']
        assert record['vm_hwm_kb']>=record['vm_rss_kb']
        assert record['build_ns']>0 and record['rows']>0
        if record['arm']=='art':
            assert record['wrap_requested_bytes']>0 and record['wrap_live_bytes']>0
            assert record['index_bytes'] is None
        if record['arm'] in ('hot','integrated','prototype'):
            assert record['wrap_requested_bytes']<=.01*(record['heap_inuse_delta']+record['heap_mmapped_delta']+1),\
                ('C++ arm should not be attributable through malloc wrapping',record['arm'])

    # 6. Decision must follow the warm-mode table, and no adoption is claimed.
    warm={c['cell']:c for c in summary if c['mode']=='warm'}
    for cell,c in warm.items():
        assert close(decision['warm_full_library_ratios_to_ART'][cell],c['ratios']['integrated/ART']['median'])
        assert close(decision['warm_full_library_ratios_to_HOT'][cell],c['ratios']['integrated/HOT']['median'])
    assert decision['ART_within_10_percent_all_E2_cells']==all(
        c['ratios']['integrated/ART']['median']<=1.10 for c in warm.values())
    assert decision['production_adopted'] is False
    assert decision['E1_general_gate_failure_preserved'] is True
    assert decision['million_unique_keys_covered'] is False
    assert decision['in_range_perturbation_and_boundary_misses_covered'] is False

    verification={'status':'verified','cells':len(summary),'timing_rows':len(raw),'gate_rows':len(heads),
                  'ledger_rows':len(ledger),'workloads':[list(x) for x in notes],
                  'checks':['sources','artifacts','inputs','pairing','checksums','ratios','flags','ns_per_op',
                            'gates','process_gates','ledger','decision']}
    (out/'verification.json').write_text(json.dumps(verification,indent=2)+'\n')
    print(json.dumps(verification['status']),len(raw),'rows',len(gates),'gates',len(ledger),'ledger rows')


if __name__=='__main__':main()

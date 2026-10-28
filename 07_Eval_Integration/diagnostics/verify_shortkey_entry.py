"""Independent C2 entry re-check: rebuild phase ratios and re-read the ledger."""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
ARMS=['region','window','window_AA','full','full_AA','directory','ART','HOT']


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text())
def close(a,b,tol=1e-12):return abs(a-b)<=tol*max(1.0,abs(a),abs(b))


def main():
    p=argparse.ArgumentParser();p.add_argument('out');a=p.parse_args()
    out=Path(a.out).resolve()
    identity=read(out/'identity.json');summary=read(out/'summary.json')
    raw=read(out/'raw.json');ledger=read(out/'ledger.json');san=read(out/'ledger_sanitized.json')
    decision=read(out/'decision.json')
    notes=[]

    for path,h in identity['sources'].items():assert sha(WORKSPACE/path)==h,('source drift',path)
    for path,h in identity['artifacts'].items():assert sha(out/path)==h,('artifact drift',path)
    for cell,item in identity['inputs'].items():
        assert sha(WORKSPACE/item['rows_path'])==item['rows_sha256']
        assert sha(WORKSPACE/item['queries_path'])==item['queries_sha256']

    assert len(raw)==4*5*48
    for cell,item in identity['inputs'].items():
        rows=[r for r in raw if r['cell']==cell]
        if not rows:continue
        assert len(rows)==240
        assert all(r['operations']==item['queries'] and r['elapsed_ns']>0 for r in rows)
        for process in range(5):
            block=[r for r in rows if r['process']==process]
            assert len(block)==48
            for repeat in range(6):
                seq=sorted((r for r in block if r['repeat']==repeat),key=lambda r:r['position'])
                assert [r['position'] for r in seq]==list(range(8))
                assert {r['arm'] for r in seq}==set(ARMS)
                # The two duplicated arms must agree bit for bit: they run the same call.
                assert len({r['checksum'] for r in seq if r['arm'] in ('full','full_AA')})==1
                assert len({r['checksum'] for r in seq if r['arm'] in ('window','window_AA')})==1
            for arm in ARMS:assert sum(1 for r in block if r['arm']==arm)==6

    for recorded in summary:
        cell=recorded['cell'];rows=[r for r in raw if r['cell']==cell]
        ns={arm:statistics.median(r['elapsed_ns']/r['operations'] for r in rows if r['arm']==arm) for arm in ARMS}
        for arm,value in recorded['ns_per_op'].items():assert close(value,ns[arm])
        ratios={}
        for arm in ARMS:
            if arm=='full':continue
            per=[]
            for process in range(5):
                t={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
                per.append(statistics.median(t[rep,arm]/t[rep,'full'] for rep in range(6)))
            ratios[arm+'/full']={'process_medians':per,'median':statistics.median(per),
                'faster':sum(x<1 for x in per),'slower':sum(x>1 for x in per)}
        bands={}
        bands['full_AA/full']=max(max(x,1/x) for x in ratios['full_AA/full']['process_medians'])
        aa_window=[]
        for process in range(5):
            t={(r['repeat'],r['arm']):r['elapsed_ns'] for r in rows if r['process']==process}
            aa_window.append(statistics.median(t[rep,'window_AA']/t[rep,'window'] for rep in range(6)))
        bands['window_AA/window']=max(max(x,1/x) for x in aa_window)
        ratios['window_AA/window']={'process_medians':aa_window,'median':statistics.median(aa_window),
            'faster':sum(x<1 for x in aa_window),'slower':sum(x>1 for x in aa_window)}
        band=max(bands.values())
        assert close(recorded['observed_band'],band)
        for name,r in ratios.items():
            r['clear_gain']=r['median']<=.95 and r['faster']>=4 and 1/r['median']>band
            r['clear_regression']=r['median']>1.05 and r['slower']>=4 and r['median']>band
            got=recorded['ratio_to_full'][name]
            assert close(got['median'],r['median']) and got['faster']==r['faster'] and got['slower']==r['slower']
            assert got['clear_gain']==r['clear_gain'] and got['clear_regression']==r['clear_regression']
        notes.append(('phase',cell,round(ratios['region/full']['median'],3),
                      round(ratios['window/full']['median'],3),round(band,3)))

    assert len(ledger)==5
    for record in ledger:
        assert record['checked_keys']==2*(identity['inputs'][record['cell']]['rows'])
        assert record['locations']==record['checked_keys'] and record['windows']==record['checked_keys']
        assert record['certificate_probes']>0
        for name,stats in record['counter'].items():
            # The driver prints means with six significant digits; that is the tolerance.
            assert abs(stats['mean']-stats['total']/record['queries'])<=1e-5*max(1.0,abs(stats['mean'])),name
            assert stats['median']<=stats['p95']<=stats['max'],name
    assert len(san)==2
    for record in san:
        normal=next(x for x in ledger if x['cell']==record['cell'])
        assert record['checked_keys']==normal['checked_keys']
        assert record['counter']['window_blocks']['total']==normal['counter']['window_blocks']['total']
        assert record['counter']['tag_block_loads']['total']==normal['counter']['tag_block_loads']['total']
        assert record['counter']['fence_steps']['total']==normal['counter']['fence_steps']['total']

    main_cell=next(c for c in summary if c['cell']=='tranco64_norepl')
    r=main_cell['ratio_to_full']
    assert close(decision['tranco64_nav_share'],1-r['region/full']['median'])
    assert close(decision['tranco64_window_selection_share'],
                 r['window/full']['median']-r['region/full']['median'])
    assert close(decision['tranco64_verify_share'],1-r['window/full']['median'])
    assert decision['production_adopted'] is False and decision['candidate_built'] is False
    assert decision['model_parameter_search_allowed'] is False
    assert decision['root_navigation_prototype_justified']==(
        decision['tranco64_nav_share']>=0.45 and decision['tranco64_nav_share']>decision['observed_band']
        and not decision['exact_positioning_is_faster'])

    verification={'status':'verified','phase_cells':len(summary),'timing_rows':len(raw),
                  'ledger_cells':len(ledger),'sanitized_ledgers':len(san),
                  'checks':['sources','artifacts','inputs','structure','duplicate_arms','ratios','bands',
                            'ledger_totals','sanitized_ledger','decision']}
    (out/'verification.json').write_text(json.dumps(verification,indent=2)+'\n')
    print(json.dumps(verification['status']),len(raw),'rows',len(ledger),'ledger',len(san),'san')


if __name__=='__main__':main()

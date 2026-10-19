"""Reconstruct the same ledger decisions, plus the constructor-only differential invariants."""
from pathlib import Path
import sys

diag=Path(__file__).resolve().parent
source=(diag/'verify_bm_space_gen.py').read_text(encoding='utf-8')
changes={
    "arms=['bm_base','native','ART']":"arms=['bm_bounded','bm_base','ART']",
    "if r['arm']=='bm_base':":"if r['arm'] in ('bm_base','bm_bounded'):",
    "'status':'stage4 ledger complete'":"'status':'stage4 bounded-build ledger complete'",
}
for old,new in changes.items():
    assert source.count(old)==1,old
    source=source.replace(old,new)
exec(compile(source,str(Path(__file__).resolve()),'exec'))

# The base verifier has already checked source, binary, input, raw-record and release identities.
out=Path(sys.argv[1]).resolve()
layouts=read(out/'layout_gates.json')
assert len(layouts)==8
for cell in ['english_words_s20261019','memetracker_urls_s20261019',
             'memetracker_phrases_s20261019','english_words_100000_s20261019']:
    rs=[r for r in layouts if r['cell']==cell]
    assert {r['flavor'] for r in rs}=={'clean','san'}
    assert {k:v for k,v in rs[0].items() if k!='flavor'}=={k:v for k,v in rs[1].items() if k!='flavor'}
    assert rs[0]['layout_checks']>130000 and rs[0]['same_fences'] and rs[0]['same_models']
for row in read(out/'summary.json'):
    bounded=row['arms']['bm_bounded'];base=row['arms']['bm_base']
    assert bounded['owned_requested']==base['owned_requested'],('resident drift',row['cell'])
    assert bounded['build_peak_requested']<base['build_peak_requested'],('peak not reduced',row['cell'])
    assert bounded['scan_output_requested']==base['scan_output_requested']
    assert bounded['find_allocations']==base['find_allocations']==0
meta=read(out/'identity.json')
prior=read(ROOT/'results/upstream_update_audit_20261019c/identity.json')
reused=0
for rel,digest in meta['sources'].items():
    if '/upstream_snapshots/' in rel and rel in prior['sources']:
        assert digest==prior['sources'][rel],('reused library source drift',rel);reused+=1
verification=read(out/'verification.json')
verification.update(layout_gate_runs=8,layout_checks_total=sum(r['layout_checks'] for r in layouts),
                    same_resident_requested=True,reduced_build_peak_all_cells=True,reused_library_source_matches=reused)
save(out/'verification.json',verification)
print('bounded constructor differential verified',json.dumps(verification))

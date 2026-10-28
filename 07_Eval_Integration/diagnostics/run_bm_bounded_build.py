"""Reuse the frozen ledger protocol with only the constructor candidate and arm list changed."""
from pathlib import Path
from generate_bm_bounded_build import index_rendered,adapter_rendered,ledger_rendered

diag=Path(__file__).resolve().parent
for name,render in [('bm_bounded_index.hpp',index_rendered),('bm_bounded_adapter.hpp',adapter_rendered),('ledger_bm_bounded.cpp',ledger_rendered)]:
    assert (diag/name).read_text(encoding='utf-8')==render(),name
source=(diag/'run_bm_space.py').read_text(encoding='utf-8')
changes={
    "ARMS=['bm_base','native','ART']":"ARMS=['bm_bounded','bm_base','ART']",
    "default='bm_space_20261019'":"default='bm_bounded_build_20261019'",
    "DIAG/'ledger_bm_space.cpp'":"DIAG/'ledger_bm_bounded.cpp'",
    "DIAG/'verify_bm_space.py'":"DIAG/'verify_bm_bounded_build.py'",
    "sources.update(WS/p for p in fixture['sources'])":
        "sources.update(WS/p for p in fixture['sources'])\n"
        "    sources.update(DIAG/p for p in ['run_bm_space.py','verify_bm_space.py','generate_bm_bounded_build.py','BM_BOUNDED_BUILD_SPEC_20261019.md'])",
    "gates=[];baseline={};contracts=[]":"gates=[];baseline={};contracts=[];layouts=[]",
    "for flavor in ['clean','san']:\n        for arm in ARMS:":
        "for flavor in ['clean','san']:\n"
        "        for cell in CELLS:\n"
        "            x=inputs[cell]\n"
        "            raw=run(['taskset','-c',meta['cpu'],build/flavor,'--layout',WS/x['rows_path'],WS/x['queries_path']],env)\n"
        "            r=json.loads(raw.splitlines()[-1]);r.update(cell=cell,flavor=flavor)\n"
        "            assert r['rows']==x['rows'] and r['queries']==x['queries'] and r['pages']==32\n"
        "            layouts.append(r);save(out/'layout_gates.json',layouts)\n"
        "            print('layout',flavor,cell,'passed',flush=True)\n"
        "        for arm in ARMS:",
}
for old,new in changes.items():
    assert source.count(old)==1,old
    source=source.replace(old,new)
exec(compile(source,str(Path(__file__).resolve()),'exec'))

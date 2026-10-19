"""Run the bounded-build candidate on the independent sample and the scale cell."""
from pathlib import Path

DIAG = Path(__file__).resolve().parent
source = (DIAG / 'run_bm_space.py').read_text(encoding='utf-8')
changes = {
    "CELLS=['english_words','memetracker_urls','memetracker_phrases','tranco64_norepl']":
        "CELLS=['english_words_s20261019','memetracker_urls_s20261019','memetracker_phrases_s20261019','english_words_100000_s20261019']",
    "    inputs=prior['inputs']":
        "    inputs=read(ROOT/'results/bm_space_gen_inputs_20261019/identity.json')['inputs']",
    "default='bm_space_20261019'": "default='bm_bounded_gen_20261019'",
    "ARMS=['bm_base','native','ART']": "ARMS=['bm_bounded','bm_base','ART']",
    "DIAG/'ledger_bm_space.cpp'": "DIAG/'ledger_bm_bounded.cpp'",
    "DIAG/'verify_bm_space.py'": "DIAG/'verify_bm_space_gen_bounded.py'",
    "sources.update(WS/p for p in fixture['sources'])":
        "sources.update(WS/p for p in fixture['sources'])\n"
        "    sources.update(DIAG/p for p in ['run_bm_space.py','verify_bm_space.py','generate_bm_bounded_build.py','BM_BOUNDED_BUILD_SPEC_20261019.md'])",
    "gates=[];baseline={};contracts=[]": "gates=[];baseline={};contracts=[];layouts=[]",
    "for flavor in ['clean','san']:\n        for arm in ARMS:":
        "for flavor in ['clean','san']:\n"
        "        for cell in CELLS:\n"
        "            x=inputs[cell]\n"
        "            raw=run(['taskset','-c',meta['cpu'],build/flavor,'--layout',WS/x['rows_path'],WS/x['queries_path']],env)\n"
        "            r=json.loads(raw.splitlines()[-1]);r.update(cell=cell,flavor=flavor)\n"
        "            assert r['rows']==x['rows'] and r['queries']==x['queries']\n"
        "            layouts.append(r);save(out/'layout_gates.json',layouts)\n"
        "            print('layout',flavor,cell,'passed',flush=True)\n"
        "        for arm in ARMS:",
}
for old, new in changes.items():
    assert source.count(old) == 1, old
    source = source.replace(old, new)
exec(compile(source, str(Path(__file__).resolve()), 'exec'))

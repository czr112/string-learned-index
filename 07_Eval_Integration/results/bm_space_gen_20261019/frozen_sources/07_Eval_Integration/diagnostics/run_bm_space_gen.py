"""Run the frozen space/build protocol on an independent sample plus one scale cell.

Reuses the same ledger, arms and gate as bm_space/bm_bounded_build; only CELLS, the input
identity and the output name change.
"""
from pathlib import Path

DIAG = Path(__file__).resolve().parent
source = (DIAG / 'run_bm_space.py').read_text(encoding='utf-8')
changes = {
    "CELLS=['english_words','memetracker_urls','memetracker_phrases','tranco64_norepl']":
        "CELLS=['english_words_s20261019','memetracker_urls_s20261019','memetracker_phrases_s20261019','english_words_100000_s20261019']",
    "    inputs=prior['inputs']":
        "    inputs=read(ROOT/'results/bm_space_gen_inputs_20261019/identity.json')['inputs']",
    "default='bm_space_20261019'": "default='bm_space_gen_20261019'",
    "'one scale, 65536 unique keys, four fixed cells'":
        "'independent sampler seed (three real types at 65536) plus one 100000-key scale cell; "
        "the unchanged sampler caps count at 100000'",
    "DIAG/'verify_bm_space.py'": "DIAG/'verify_bm_space_gen.py'",
}
for old, new in changes.items():
    assert source.count(old) == 1, old
    source = source.replace(old, new)
exec(compile(source, str(Path(__file__).resolve()), 'exec'))

"""N5: 100k-key dynamic re-verification, reusing the N4 generator and runner."""
from pathlib import Path

DIAG = Path(__file__).resolve().parent
source = (DIAG / 'run_bm_n4.py').read_text(encoding='utf-8')
changes = {
    "DATA = {'english_words': '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',\n"
    "        'memetracker_urls': '07_Eval_Integration/results/bm_real_20261018/interchange/memetracker_urls_rows.bin'}":
        "DATA = {'english_words_100k': "
        "'07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/"
        "english_words_100000_s20261019_rows.bin'}",
    "        for ratio, find_writes in [('95_5', Fraction(1, 20)), ('80_20', Fraction(1, 5))]:\n"
    "            for locality in ('uniform', 'hotspot'):\n"
    "                for semantics in ('growth', 'replacement'):":
        "        for ratio, find_writes in [('95_5', Fraction(1, 20)), ('80_20', Fraction(1, 5))]:\n"
        "            for locality in ('uniform', 'hotspot'):\n"
        "                for semantics in ('growth',):",
    "default='bm_n4_matrix_20261022'": "default='bm_n5_100k_20261023'",
}
for old, new in changes.items():
    assert source.count(old) == 1, old[:60]
    source = source.replace(old, new)
exec(compile(source, str(Path(__file__).resolve()), 'exec'))

"""Independent re-check of the native-reference round."""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases']
ARMS = ['native-delta', 'native-delta_AA', 'native-shortdir-first', 'native-shortdir-first_AA',
        'bm-page-window', 'bm-page-window_AA', 'bm-page-base', 'F5', 'ART']
PAIRS = [('native-delta', 'ART'), ('native-shortdir-first', 'ART'), ('native-delta', 'bm-page-window'),
         ('native-shortdir-first', 'native-delta'), ('bm-page-window', 'ART'), ('F5', 'ART'),
         ('native-delta_AA', 'native-delta'), ('native-shortdir-first_AA', 'native-shortdir-first'),
         ('bm-page-window_AA', 'bm-page-window')]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-12): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); summary = read(out / 'summary.json')
    raw = read(out / 'raw.json'); gates = read(out / 'process_gates.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WORKSPACE))
    spec = '07_Eval_Integration/diagnostics/BM_NATIVE_SPEC_20261018.md'
    editable = {own, spec}
    run_hashes = {p: identity['sources'].get(p, '') for p in editable}
    for path, digest in identity['sources'].items():
        if path in editable:
            continue
        assert sha(WORKSPACE / path) == digest, ('source drift', path)
    for path, digest in identity['artifacts'].items(): assert sha(out / path) == digest, ('artifact drift', path)
    for cell, item in identity['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']

    assert len(raw) == 3 * 5 * 54 and len(gates) == 3 * 5 * 9
    for cell in CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 270
        expected = identity['inputs'][cell]['queries']
        assert all(r['operations'] == expected for r in rows)
        for process in range(5):
            block = [r for r in rows if r['process'] == process]
            assert len(block) == 54
            for repeat in range(6):
                seq = sorted((r for r in block if r['repeat'] == repeat), key=lambda r: r['position'])
                assert [r['position'] for r in seq] == list(range(9))
                assert {r['arm'] for r in seq} == set(ARMS)
                assert len({r['checksum'] for r in seq}) == 1
            for arm in ARMS: assert sum(1 for r in block if r['arm'] == arm) == 6
    for gate in gates:
        item = identity['inputs'][gate['cell']]
        assert gate['verified'] == item['rows'] + item['queries']
    names = {g['arm']: g['implementation'] for g in gates}
    assert names['native-delta'] == 'block-residual-delta'
    assert names['native-shortdir-first'] == 'block-residual-delta-shortdir-first'
    assert names['bm-page-window'].startswith('BM-page')
    assert names['F5'].startswith('F5')
    assert 'libart' in names['ART']

    for recorded in summary:
        cell = recorded['cell']; rows = [r for r in raw if r['cell'] == cell]
        ratios = {}
        for x, y in PAIRS:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            ratios[x + '/' + y] = {'process_medians': medians, 'median': statistics.median(medians),
                                   'faster': sum(v < 1 for v in medians), 'slower': sum(v > 1 for v in medians)}
        bands = {name: max(max(v, 1 / v) for v in ratios[name]['process_medians'])
                 for name in ('native-delta_AA/native-delta', 'native-shortdir-first_AA/native-shortdir-first',
                              'bm-page-window_AA/bm-page-window')}
        band = max(bands.values())
        assert close(recorded['observed_band'], band)
        for name, value in ratios.items():
            value['clear_gain'] = value['median'] <= .95 and value['faster'] >= 4 and 1 / value['median'] > band
            value['clear_regression'] = value['median'] > 1.05 and value['slower'] >= 4 and value['median'] > band
            got = recorded['ratios'][name]
            assert close(got['median'], value['median']) and got['faster'] == value['faster']
            assert got['clear_gain'] == value['clear_gain'] and got['clear_regression'] == value['clear_regression']

    art = {c['cell']: c['ratios']['native-delta/ART']['median'] for c in summary}
    for cell, value in art.items(): assert close(dec['native_delta_over_ART'][cell], value)
    assert dec['ART_gate_all_cells'] == all(v <= 1.10 for v in art.values())
    assert dec['complete_factory_beats_bm_page'] == all(
        c['ratios']['native-delta/bm-page-window']['median'] < 1 for c in summary)
    assert dec['production_adopted'] is False and dec['default_changed'] is False
    assert dec['hot_included'] is False

    verification = {'status': 'verified', 'cells': len(summary), 'timing_rows': len(raw), 'gate_rows': len(gates),
                    'native_delta_over_ART': art,
                    'editable_sources': {p: {'current': sha(WORKSPACE / p), 'recorded_at_run': run_hashes[p]}
                                         for p in sorted(editable)},
                    'checks': ['sources', 'artifacts', 'inputs', 'structure', 'checksums', 'gate_identity',
                               'ratios', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), 'cells', len(summary), 'rows', len(raw),
          'native/ART', {c: round(v, 3) for c, v in art.items()})


if __name__ == '__main__':
    main()

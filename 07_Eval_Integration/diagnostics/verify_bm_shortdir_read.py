"""Independent re-check of the short-directory full read gate."""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases', 'tranco64_norepl']
REAL = CELLS[:3]
ARMS = ['shortdir', 'shortdir_AA', 'bm-page-window', 'bm-page-window_AA', 'bm-page-base',
        'native-shortdir-first', 'ART', 'F5']
PAIRS = [('shortdir', 'ART'), ('shortdir', 'bm-page-window'), ('shortdir', 'native-shortdir-first'),
         ('bm-page-window', 'ART'), ('native-shortdir-first', 'ART'), ('F5', 'ART'),
         ('shortdir_AA', 'shortdir'), ('bm-page-window_AA', 'bm-page-window')]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-12): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); summary = read(out / 'summary.json')
    raw = read(out / 'raw.json'); domains = read(out / 'domain.json')
    san = read(out / 'domain_sanitized.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WORKSPACE))
    spec = '07_Eval_Integration/diagnostics/BM_SHORTDIR_READ_SPEC_20261018.md'
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

    assert [d['cell'] for d in domains] == CELLS
    for d in domains:
        assert d['mismatches'] == 0
        assert d['find_checks'] > 0 and d['scans'] > 0
        assert d['memory']['total'] > d['memory']['short_directory'] > 0
        assert d['memory']['fence_views'] > 0
    assert len(san) == 2
    for s in san:
        normal = next(d for d in domains if d['cell'] == s['cell'])
        assert s['mismatches'] == 0 and s['checks'] == normal['checks'] and s['memory'] == normal['memory']

    assert len(raw) == 4 * 5 * 48
    for cell in CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 240
        expected = identity['inputs'][cell]['queries']
        assert all(r['operations'] == expected for r in rows)
        for process in range(5):
            block = [r for r in rows if r['process'] == process]
            assert len(block) == 48
            for repeat in range(6):
                seq = sorted((r for r in block if r['repeat'] == repeat), key=lambda r: r['position'])
                assert [r['position'] for r in seq] == list(range(8))
                assert {r['arm'] for r in seq} == set(ARMS)
                assert len({r['checksum'] for r in seq}) == 1
            for arm in ARMS: assert sum(1 for r in block if r['arm'] == arm) == 6

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
                 for name in ('shortdir_AA/shortdir', 'bm-page-window_AA/bm-page-window')}
        band = max(bands.values())
        assert close(recorded['observed_band'], band)
        for name, value in ratios.items():
            value['clear_gain'] = value['median'] <= .95 and value['faster'] >= 4 and 1 / value['median'] > band
            value['clear_regression'] = value['median'] > 1.05 and value['slower'] >= 4 and value['median'] > band
            got = recorded['ratios'][name]
            assert close(got['median'], value['median']) and got['faster'] == value['faster']
            assert got['clear_gain'] == value['clear_gain'] and got['clear_regression'] == value['clear_regression']
        for arm, ns in recorded['ns_per_op'].items():
            assert close(ns, statistics.median(r['elapsed_ns'] / r['operations'] for r in rows if r['arm'] == arm))

    art = {c['cell']: c['ratios']['shortdir/ART']['median'] for c in summary}
    for cell, value in art.items(): assert close(dec['shortdir_over_ART'][cell], value)
    assert dec['real_cells_within_1_10'] == all(art[c] <= 1.10 for c in REAL)
    assert dec['all_cells_within_1_10'] == all(v <= 1.10 for v in art.values())
    by = {d['cell']: d for d in domains}
    for cell in CELLS:
        added = by[cell]['memory']['short_directory'] + by[cell]['memory']['fence_views']
        assert close(dec['metadata_added_share'][cell], added / by[cell]['memory']['total'])
    assert dec['metadata_within_5_percent'] == all(v <= 0.05 for v in dec['metadata_added_share'].values())
    assert dec['beats_replaced_page_path'] == all(
        c['ratios']['shortdir/bm-page-window']['median'] < 1 for c in summary)
    assert dec['production_adopted'] is False and dec['default_changed'] is False

    verification = {'status': 'verified', 'cells': len(summary), 'timing_rows': len(raw),
                    'domain_cells': len(domains), 'sanitized_domains': len(san),
                    'shortdir_over_ART': art,
                    'editable_sources': {p: {'current': sha(WORKSPACE / p), 'recorded_at_run': run_hashes[p]}
                                         for p in sorted(editable)},
                    'checks': ['sources', 'artifacts', 'inputs', 'domain', 'sanitized_domain', 'structure',
                               'checksums', 'ratios', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), 'cells', len(summary), 'rows', len(raw),
          'shortdir/ART', {c: round(v, 3) for c, v in art.items()})


if __name__ == '__main__':
    main()

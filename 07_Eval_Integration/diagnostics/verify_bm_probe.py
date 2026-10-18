"""Independent re-check of the payload-probe entry."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases', 'tranco64_norepl']
MAIN = 'english_words'


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-12): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); counts = read(out / 'counts.json')
    san = read(out / 'counts_sanitized.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WORKSPACE))
    spec = '07_Eval_Integration/diagnostics/BM_PROBE_SPEC_20261018.md'
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

    assert [r['cell'] for r in counts] == CELLS
    for record in counts:
        item = identity['inputs'][record['cell']]
        assert record['mismatch'] == 0
        assert record['keys_checked'] == item['rows'] + item['queries']
        assert record['pages'] == (item['rows'] + 2047) // 2048
        assert record['current']['blocks'] == item['queries']
        assert record['probe']['calls'] == item['queries']
        assert record['probe']['hits'] + record['probe']['misses'] == record['probe']['calls']
        # The driver prints six significant digits.
        assert close(record['probe']['hit_rate'], record['probe']['hits'] / record['probe']['calls'], 1e-5)
        assert record['probe']['fallbacks'] == record['probe']['misses']
        # Probe blocks plus fallback blocks is what a query actually touches.
        assert record['probe']['blocks'] >= record['probe']['probe_block_loads']
        for path in ('current', 'probe'):
            r = record[path]
            assert close(r['fence_per_query'], r['fence_compares'] / item['queries'], 1e-5)
            assert close(r['key_per_query'], r['key_compares'] / item['queries'], 1e-5)
        assert close(record['delta']['fence_per_query'],
                     record['probe']['fence_per_query'] - record['current']['fence_per_query'], 1e-5)
        assert close(record['delta']['blocks_per_query'],
                     record['probe']['blocks_per_query'] - record['current']['blocks_per_query'], 1e-5)

    assert len(san) == 2
    for record in san:
        normal = next(x for x in counts if x['cell'] == record['cell'])
        for path in ('current', 'probe'):
            assert record[path] == normal[path]

    for cell in CELLS:
        record = next(x for x in counts if x['cell'] == cell)
        expected = ((record['probe']['fence_per_query'] - record['current']['fence_per_query'])
                    / record['current']['fence_per_query'])
        assert close(dec['fence_reduction'][cell], expected, 1e-5)
        assert close(dec['probe_hit_rate'][cell], record['probe']['hit_rate'])
    assert dec['entry_gate_25_percent'] == (-dec['fence_reduction'][MAIN] >= 0.25)
    assert dec['entry_passes'] == (dec['entry_gate_25_percent'] and dec['never_amplifies_blocks'])
    assert dec['timing_round_allowed'] is False and dec['candidate_closed'] is True
    assert dec['stop_prediction_family'] is True and dec['prediction_family_failures'] == 2
    assert dec['production_adopted'] is False

    verification = {'status': 'verified', 'cells': len(counts), 'sanitized_cells': len(san),
                    'editable_sources': {p: {'current': sha(WORKSPACE / p), 'recorded_at_run': run_hashes[p]}
                                         for p in sorted(editable)},
                    'checks': ['sources', 'artifacts', 'inputs', 'counters', 'sanitized', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), len(counts), 'cells',
          'main fence reduction', round(-dec['fence_reduction'][MAIN], 3),
          'block growth', round(dec['block_growth_per_query'][MAIN], 3))


if __name__ == '__main__':
    main()

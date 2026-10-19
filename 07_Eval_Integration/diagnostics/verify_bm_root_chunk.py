"""Independent re-check of the root chunk entry."""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
ARMS = ['flat', 'chunked', 'flat_AA', 'chunked_AA']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-9): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); summary = read(out / 'summary.json')
    raw = read(out / 'raw.json'); gates = read(out / 'gates.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WS))
    spec = '07_Eval_Integration/diagnostics/BM_ROOT_CHUNK_SPEC_20261019.md'
    editable = {own, spec}
    for path, digest in identity['sources'].items():
        if path in editable:
            continue
        assert sha(WS / path) == digest, ('source drift', path)
    for path, digest in identity['artifacts'].items(): assert sha(out / path) == digest, ('artifact drift', path)
    for label, item in identity['inputs'].items():
        assert sha(WS / item['rows_path']) == item['rows_sha256']
        assert sha(WS / item['queries_path']) == item['queries_sha256']

    assert len(gates) == 3
    for gate in gates:
        assert gate['mismatches'] == 0 and gate['checked'] > 0
        assert gate['chunks'] == (gate['pages'] + gate['chunk_size'] - 1) // gate['chunk_size']
        pub = gate['publication']
        assert pub['chunked_entries'] == gate['chunks'] + gate['chunk_size']
        assert pub['flat_entries'] == gate['pages']
        assert close(pub['entry_reduction'], 1 - pub['chunked_entries'] / pub['flat_entries'], 1e-5)
        assert close(pub['byte_reduction'], 1 - pub['chunked_key_bytes'] / pub['flat_key_bytes'], 1e-5)

    assert len(raw) == 3 * 5 * 24
    for recorded in summary:
        rows = [r for r in raw if r['cell'] == recorded['cell']]
        assert len(rows) == 120
        for process in range(5):
            block = [r for r in rows if r['process'] == process]
            assert len(block) == 24
            for repeat in range(6):
                seq = sorted((r for r in block if r['repeat'] == repeat), key=lambda r: r['position'])
                assert [r['position'] for r in seq] == list(range(4))
                assert {r['arm'] for r in seq} == set(ARMS)
                assert len({r['checksum'] for r in seq}) == 1
        for name, x, y in [('chunked/flat', 'chunked', 'flat'),
                           ('flat_AA/flat', 'flat_AA', 'flat'),
                           ('chunked_AA/chunked', 'chunked_AA', 'chunked')]:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            assert close(recorded['ratios'][name]['median'], statistics.median(medians), 1e-12)
            assert recorded['ratios'][name]['faster'] == sum(v < 1 for v in medians)

    for cell, value in dec['routing_ratio'].items(): assert close(value, 1.0, 1.0) and value > 0
    assert dec['routing_within_5_percent'] == all(abs(v - 1) <= 0.05 for v in dec['routing_ratio'].values())
    assert dec['publication_cut_at_least_25_percent'] == all(
        v >= 0.25 for v in dec['publication_entry_reduction'].values())
    assert dec['entry_gate_passed'] == (dec['routing_within_5_percent'] and dec['publication_cut_at_least_25_percent'])
    assert dec['integration_attempted'] is False and dec['production_adopted'] is False

    verification = {'status': 'verified', 'cells': len(summary), 'timing_rows': len(raw),
                    'routing_within_5_percent': dec['routing_within_5_percent'],
                    'publication_cut_at_least_25_percent': dec['publication_cut_at_least_25_percent'],
                    'checks': ['sources', 'artifacts', 'inputs', 'gate', 'publication arithmetic',
                               'pairing', 'checksums', 'ratios', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), len(summary), 'cells',
          'routing<5%', dec['routing_within_5_percent'],
          'pub>=25%', dec['publication_cut_at_least_25_percent'])


if __name__ == '__main__':
    main()

"""Independent re-check of the dynamic read-budget round."""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
CELLS = ['english_words', 'english_words_80_20', 'memetracker_urls', 'memetracker_urls_80_20']
ARMS = ['dynamic', 'dynamic_AA', 'snapshot', 'snapshot_AA']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-9): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); summary = read(out / 'summary.json')
    raw = read(out / 'raw.json'); gates = read(out / 'gates.json')
    san = read(out / 'gates_sanitized.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WS))
    spec = '07_Eval_Integration/diagnostics/BM_DYNAMIC_READ_SPEC_20261019.md'
    editable = {own, spec}
    for path, digest in identity['sources'].items():
        if path in editable:
            continue
        assert sha(WS / path) == digest, ('source drift', path)
    for path, digest in identity['artifacts'].items(): assert sha(out / path) == digest, ('artifact drift', path)
    for cell, item in identity['traces'].items():
        assert sha(WS / item['rows_path']) == item['rows_sha256']
        assert sha(WS / item['operations_path']) == item['operations_sha256']

    assert len(gates) == 4 and [g['cell'] for g in gates] == CELLS
    for g in gates:
        assert g['mismatches'] == 0
        # One duplicate insert can be refused, so the growth is between zero and insert_ops.
        assert g['initial_rows'] <= g['final_rows'] <= g['initial_rows'] + g['insert_ops']
        assert g['maintenance']['root_publications'] >= g['maintenance']['rebuilds']
        assert g['maintenance']['largest_root_entries'] <= g['maintenance']['pages']
        assert g['pause_ns']['p50'] <= g['pause_ns']['p95'] <= g['pause_ns']['p99'] <= g['pause_ns']['max']
    assert len(san) == 2
    for record in san:
        normal = next(x for x in gates if x['cell'] == record['cell'])
        for field in ('initial_rows', 'operations', 'final_rows', 'mismatches', 'maintenance', 'memory'):
            assert record[field] == normal[field], (record['cell'], field)

    assert len(raw) == 4 * 5 * 24
    for cell in CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 120
        for process in range(5):
            block = [r for r in rows if r['process'] == process]
            assert len(block) == 24
            for repeat in range(6):
                seq = sorted((r for r in block if r['repeat'] == repeat), key=lambda r: r['position'])
                assert [r['position'] for r in seq] == list(range(4))
                assert {r['arm'] for r in seq} == set(ARMS)
                assert len({r['checksum'] for r in seq}) == 1
            for arm in ARMS: assert sum(1 for r in block if r['arm'] == arm) == 6

    for recorded in summary:
        cell = recorded['cell']; rows = [r for r in raw if r['cell'] == cell]
        for name, x, y in [('snapshot/dynamic', 'snapshot', 'dynamic'),
                           ('dynamic_AA/dynamic', 'dynamic_AA', 'dynamic'),
                           ('snapshot_AA/snapshot', 'snapshot_AA', 'snapshot')]:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            got = recorded['ratios'][name]
            assert close(got['median'], statistics.median(medians), 1e-12)
            assert got['faster'] == sum(v < 1 for v in medians)
        assert close(dec['snapshot_over_dynamic'][cell], recorded['ratios']['snapshot/dynamic']['median'], 1e-12)
        assert dec['read_budget_1_15'][cell] == (recorded['ratios']['snapshot/dynamic']['median'] <= 1.15)
        assert dec['root'][cell]['pages'] == next(g for g in gates if g['cell'] == cell)['maintenance']['pages']
    assert dec['all_within_budget'] == all(dec['read_budget_1_15'].values())
    assert dec['mismatches'] == 0
    assert dec['paired_timing_done'] is True and dec['production_adopted'] is False

    verification = {'status': 'verified', 'cells': len(summary), 'timing_rows': len(raw),
                    'gate_rows': len(gates), 'sanitized_cells': len(san),
                    'read_budget_1_15': dec['read_budget_1_15'],
                    'checks': ['sources', 'artifacts', 'traces', 'gate', 'sanitized', 'pairing', 'checksums',
                               'ratios', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), len(summary), 'cells',
          'budget', dec['read_budget_1_15'])


if __name__ == '__main__':
    main()

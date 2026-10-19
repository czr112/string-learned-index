"""Independent re-check of the stage-5 dynamic entry."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
CELLS = ['english_words', 'english_words_80_20', 'memetracker_urls', 'memetracker_urls_80_20']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-9): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); records = read(out / 'records.json')
    san = read(out / 'records_sanitized.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WS))
    for path, digest in identity['sources'].items():
        if path == own:
            continue
        assert sha(WS / path) == digest, ('source drift', path)
    for path, digest in identity['artifacts'].items(): assert sha(out / path) == digest, ('artifact drift', path)
    for cell, item in identity['traces'].items():
        assert sha(WS / item['rows_path']) == item['rows_sha256']
        assert sha(WS / item['operations_path']) == item['operations_sha256']

    assert [r['cell'] for r in records] == CELLS
    for record in records:
        item = identity['traces'][record['cell']]
        assert record['mismatches'] == 0
        assert record['initial_rows'] == item['initial_rows']
        assert record['operations'] == item['operations']
        assert record['find_ops'] == item['kinds'].get('1', 0)
        assert record['insert_ops'] == item['kinds'].get('2', 0)
        assert record['find_ops'] + record['insert_ops'] + record['update_ops'] + record['erase_ops'] + record['scan_ops'] == record['operations']
        assert record['insert_new'] + record['insert_dup'] == record['insert_ops'] or record['insert_ops'] == 0
        # A duplicate insert is refused before any maintenance, so only new keys split
        # between in-place inserts and page rebuilds.
        assert record['inplace_inserts'] + record['rebuilds'] == record['insert_new']
        assert record['root_publications'] >= record['rebuilds']
        assert record['final_size'] == record['initial_rows'] + record['insert_new']
        assert record['final_read_checked'] == record['final_size']
        assert record['final_scan_rows'] == record['final_size']
        assert record['build_ns'] > 0 and record['find_ns'] > 0

    assert len(san) == 2
    for record in san:
        normal = next(x for x in records if x['cell'] == record['cell'])
        for field in ('initial_rows', 'operations', 'find_ops', 'insert_ops', 'final_size', 'mismatches',
                      'root_publications', 'rebuilds', 'rows_rebuilt', 'inplace_inserts'):
            assert record[field] == normal[field], (record['cell'], field)

    for record in records:
        cell = dec['cells'][record['cell']]
        assert cell['find_ops'] == record['find_ops'] and cell['insert_ops'] == record['insert_ops']
        assert cell['rebuilds'] == record['rebuilds'] and cell['final_size'] == record['final_size']
        assert close(cell['find_ns_per_op'], record['find_ns'] / record['find_ops'])
        assert close(cell['write_ns_per_op'], record['write_ns'] / record['insert_ops'])
    assert dec['entry_passed'] is True
    assert dec['paired_timing_done'] is False and dec['throughput_claim'] == 'none'
    assert dec['production_adopted'] is False

    verification = {'status': 'verified', 'cells': len(records), 'sanitized_cells': len(san),
                    'checks': ['sources', 'artifacts', 'traces', 'per-op counts', 'final map', 'sanitized', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), len(records), 'traces', len(san), 'sanitized')


if __name__ == '__main__':
    main()

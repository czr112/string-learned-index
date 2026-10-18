"""Independent re-check of the BM bracket entry: rebuild every ratio from counts.json."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
CELLS = ['tranco64_norepl', 'tranco64_miss', 'mixed_full_norepl', 'skew_full_norepl', 'skew_miss']
MAIN = 'tranco64_norepl'


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-12): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); counts = read(out / 'counts.json')
    san = read(out / 'counts_sanitized.json'); dec = read(out / 'decision.json')

    for path, digest in identity['sources'].items(): assert sha(WORKSPACE / path) == digest, ('source drift', path)
    for path, digest in identity['artifacts'].items(): assert sha(out / path) == digest, ('artifact drift', path)
    for cell, item in identity['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']

    assert [r['cell'] for r in counts] == CELLS
    for record in counts:
        item = identity['inputs'][record['cell']]
        assert record['mismatch'] == {'exact': 0, 'current': 0, 'bracket': 0, 'answer': 0}
        assert record['bracket']['checks'] == item['queries'] == record['queries']
        assert record['keys_checked'] == item['rows'] + item['queries']
        assert record['memory']['frozen'] == record['memory']['copy']
        assert record['pages'] == (item['rows'] + 2047) // 2048
        # Counters must partition: boundary compares plus search steps equal the total.
        for path in ('base', 'current'):
            assert record[path]['total_compares'] == record[path]['boundary_compares'] + record[path]['directory_steps']
        assert record['current']['blocks'] == item['queries']
        assert record['bracket']['blocks'] == item['queries']
        assert (record['bracket']['hits'] + record['bracket']['left'] + record['bracket']['right']
                + record['bracket']['out_of_range']) == record['bracket']['checks']
        assert close(record['bracket']['hit_rate'],
                     record['bracket']['hits'] / record['bracket']['checks'])
        for path in ('base', 'current', 'bracket'):
            assert close(record[path]['compares_per_query'],
                         record[path]['total_compares'] / record['queries'])

    for record in san:
        normal = next(x for x in counts if x['cell'] == record['cell'])
        for path in ('base', 'current', 'bracket'):
            for field in ('total_compares', 'blocks', 'boundary_compares'):
                assert record[path][field] == normal[path][field]

    by = {r['cell']: r for r in counts}
    ratios = {c: by[c]['bracket']['compares_per_query'] / by[c]['current']['compares_per_query'] for c in CELLS}
    for cell, value in ratios.items(): assert close(dec['bracket_over_current'][cell], value)
    for cell in CELLS:
        assert close(dec['compares_per_query'][cell]['current'], by[cell]['current']['compares_per_query'])
        assert close(dec['compares_per_query'][cell]['bracket'], by[cell]['bracket']['compares_per_query'])
        assert close(dec['window_blocks_per_query'][cell], by[cell]['current']['proposal_blocks'] / by[cell]['queries'])
    assert dec['main_cell_removes_work'] == (ratios[MAIN] <= 0.90)
    assert dec['no_cell_regresses_over_5_percent'] == all(v <= 1.05 for v in ratios.values())
    assert dec['timing_round_allowed'] == dec['main_cell_removes_work'] and dec['no_cell_regresses_over_5_percent']
    assert dec['candidate_closed'] == (ratios[MAIN] > 1.00)
    assert dec['location_mismatches'] == 0 and dec['memory_equal_everywhere'] is True
    assert dec['model_parameters_searched'] is False and dec['production_adopted'] is False

    verification = {'status': 'verified', 'cells': len(counts), 'sanitized_cells': len(san),
                    'main_ratio': ratios[MAIN], 'checks': ['sources', 'artifacts', 'inputs', 'counters',
                                                           'sanitized', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), len(counts), 'cells', 'main ratio', round(ratios[MAIN], 4))


if __name__ == '__main__':
    main()

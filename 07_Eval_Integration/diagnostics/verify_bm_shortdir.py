"""Independent re-check of the short-directory ablation."""
import argparse
import hashlib
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
CELLS = ['tranco64_norepl', 'tranco64_miss', 'mixed_full_norepl', 'skew_full_norepl', 'skew_miss']
TIME_CELLS = ['tranco64_norepl', 'mixed_full_norepl', 'skew_full_norepl']
MAIN = 'tranco64_norepl'
ARMS = ['current', 'current_AA', 'short', 'short_AA', 'directory', 'directory_AA']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-12): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); counts = read(out / 'counts.json')
    san = read(out / 'counts_sanitized.json'); summary = read(out / 'summary.json')
    raw = read(out / 'raw.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WORKSPACE))
    spec = '07_Eval_Integration/diagnostics/BM_SHORTDIR_SPEC_20261018.md'
    # Two recorded files are edited after the measurement by design: the verifier (bug fix) and
    # the spec (its section 5 records the result). Their run-time hashes are reported instead.
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

    assert [c['cell'] for c in counts] == CELLS
    for record in counts:
        item = identity['inputs'][record['cell']]
        assert record['mismatch'] == {'bm': 0, 'short': 0, 'counted': 0, 'answer': 0}
        assert record['keys_checked'] == item['rows'] + item['queries']
        assert record['pages'] == (item['rows'] + 2047) // 2048
        assert record['queries'] == item['queries']
        assert record['short']['hits'] + record['short']['fallbacks'] == record['queries']
        assert close(record['short']['hit_rate'], record['short']['hits'] / record['queries'], 1e-5)
        # The counter partition must hold exactly: fence plus fallback steps plus numeric compares.
        assert record['short']['numeric_compares'] == (record['short']['search_steps']
                                                       + record['short']['bracket_compares']
                                                       + record['short']['prefix_compares'])
        assert record['short']['total_compares'] == (record['short']['fence_compares']
                                                     + record['short']['fallback_steps']
                                                     + record['short']['numeric_compares'])
        assert record['current']['total_compares'] == (record['current']['fence_compares']
                                                       + record['current']['directory_steps'])
        assert record['short']['blocks'] == record['current']['blocks'] == item['queries']

    for record in san:
        normal = next(x for x in counts if x['cell'] == record['cell'])
        for path, names in {'current': ('fence_compares', 'directory_steps', 'blocks'),
                            'short': ('fence_compares', 'search_steps', 'bracket_compares',
                                      'hits', 'fallbacks', 'blocks')}.items():
            for field in names:
                assert record[path][field] == normal[path][field], (record['cell'], path, field)

    assert len(raw) == 3 * 5 * 36
    for cell in TIME_CELLS:
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 180
        for process in range(5):
            block = [r for r in rows if r['process'] == process]
            assert len(block) == 36
            for repeat in range(6):
                seq = sorted((r for r in block if r['repeat'] == repeat), key=lambda r: r['position'])
                assert [r['position'] for r in seq] == list(range(6))
                assert {r['arm'] for r in seq} == set(ARMS)
                assert len({r['checksum'] for r in seq}) == 1
            for arm in ARMS: assert sum(1 for r in block if r['arm'] == arm) == 6

    for recorded in summary:
        cell = recorded['cell']; rows = [r for r in raw if r['cell'] == cell]
        for name, x, y in [('short/current', 'short', 'current'), ('directory/current', 'directory', 'current'),
                           ('current_AA/current', 'current_AA', 'current'), ('short_AA/short', 'short_AA', 'short')]:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            got = recorded['ratios'][name]
            assert close(got['median'], statistics.median(medians), 1e-12)
            assert got['faster'] == sum(v < 1 for v in medians) and got['slower'] == sum(v > 1 for v in medians)
        band = max(max(max(v, 1 / v) for v in recorded['ratios'][n]['process_medians'])
                   for n in ('current_AA/current', 'short_AA/short'))
        assert close(recorded['observed_band'], band)

    by = {e['cell']: e for e in counts}
    fence_ratio = {c: by[c]['short']['fence_per_query'] / (by[c]['current']['fence_compares'] / by[c]['queries'])
                   for c in CELLS}
    for cell, value in fence_ratio.items(): assert close(dec['short_fence_over_current_fence'][cell], value)
    assert dec['structural_gate_registered_all_cells'] == all(v <= 0.30 for v in fence_ratio.values())
    applicable = {c: v for c, v in fence_ratio.items() if dec['short_hit_rate'][c] > 0}
    assert dec['structural_gate_on_applicable_pages'] == all(v <= 0.30 for v in applicable.values())
    ratios = {c['cell']: c['ratios']['short/current']['median'] for c in summary}
    assert close(dec['timing_short_over_current'][MAIN], ratios[MAIN])
    assert dec['timing_gate'] == (ratios[MAIN] <= 0.90 and all(v <= 1.05 for v in ratios.values()))
    assert dec['scoped_candidate'] == dec['timing_gate']
    assert dec['candidate_for_full_read_gate'] is False and dec['full_read_gate_needs_new_registration'] is True
    assert dec['location_mismatches'] == 0
    assert dec['model_parameters_searched'] is False and dec['upstream_modified'] is False
    assert dec['production_adopted'] is False

    verification = {'status': 'verified', 'entry_cells': len(counts), 'sanitized_cells': len(san),
                    'timing_rows': len(raw), 'main_ratio': ratios[MAIN],
                    'structural_gate_registered_all_cells': dec['structural_gate_registered_all_cells'],
                    'structural_gate_on_applicable_pages': dec['structural_gate_on_applicable_pages'],
                    'timing_gate': dec['timing_gate'],
                    'editable_sources': {p: {'current': sha(WORKSPACE / p), 'recorded_at_run': run_hashes[p]}
                                         for p in sorted(editable)},
                    'checks': ['sources', 'artifacts', 'inputs', 'entry_partition', 'sanitized', 'pairing',
                               'checksums', 'ratios', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), 'main', round(ratios[MAIN], 4),
          'structural(all)', dec['structural_gate_registered_all_cells'],
          'structural(applicable)', dec['structural_gate_on_applicable_pages'],
          'timing', dec['timing_gate'])


if __name__ == '__main__':
    main()

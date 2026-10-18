"""Independent re-check of the stage-3 encoding audit."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases', 'tranco64_norepl']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def close(a, b, tol=1e-9): return abs(a - b) <= tol * max(1.0, abs(a), abs(b))


def main():
    p = argparse.ArgumentParser(); p.add_argument('out'); a = p.parse_args()
    out = Path(a.out).resolve()
    identity = read(out / 'identity.json'); audit = read(out / 'audit.json')
    san = read(out / 'audit_sanitized.json'); dec = read(out / 'decision.json')

    own = str(Path(__file__).resolve().relative_to(WORKSPACE))
    spec = '07_Eval_Integration/diagnostics/BM_ENCODING_AUDIT_SPEC_20261019.md'
    editable = {own, spec}
    run_hashes = {p: identity['sources'].get(p, '') for p in editable}
    for path, digest in identity['sources'].items():
        if path in editable:
            continue
        assert sha(WORKSPACE / path) == digest, ('source drift', path)
    for path, digest in identity['artifacts'].items(): assert sha(out / path) == digest, ('artifact drift', path)

    assert [r['cell'] for r in audit] == CELLS
    for record in audit:
        item = identity['inputs'][record['cell']]
        assert record['rows'] == item['rows'] and record['queries'] == item['queries']
        assert record['pages'] == (item['rows'] + 2047) // 2048
        assert record['blocks'] > 0
        assert record['predicted_is_containing']['keys'] == item['rows']
        assert record['predicted_is_containing']['hits'] <= item['rows']
        assert close(record['predicted_is_containing']['rate'],
                     record['predicted_is_containing']['hits'] / item['rows'], 1e-5)
        assert close(record['window_covers_containing']['rate'],
                     record['window_covers_containing']['inside'] / item['rows'], 1e-5)
        assert (record['window_covers_containing']['inside']
                + record['window_covers_containing']['outside']) == item['rows']
        absent = record['absent']
        assert absent['queries'] == item['queries'] // 10
        assert (absent['pred_inside_window'] + absent['pred_outside_window']
                + absent['prefix_rejected']) == absent['queries']
        assert close(absent['outside_rate'], absent['pred_outside_window'] / absent['queries'], 1e-5)
        collisions = record['collisions']
        assert collisions['fences_in_runs'] <= record['blocks']
        assert close(collisions['share'], collisions['fences_in_runs'] / record['blocks'], 1e-5)
        assert collisions['run_p50'] <= collisions['run_p95'] <= collisions['run_max']
        assert collisions['extra_prefix_p50'] <= collisions['extra_prefix_p95'] <= collisions['extra_prefix_max']
        assert record['window_rows']['p50'] <= record['window_rows']['p95'] <= record['window_rows']['max']

    assert len(san) == 1
    normal = next(x for x in audit if x['cell'] == san[0]['cell'])
    for field in ('rows','queries','pages','blocks','certified_pages','segments','collisions',
                  'window_rows','predicted_is_containing','absent'):
        assert san[0][field] == normal[field], field

    by = {r['cell']: r for r in audit}
    for cell in CELLS:
        assert close(dec['collision_share'][cell], by[cell]['collisions']['share'])
        assert dec['extra_prefix_median'][cell] == by[cell]['collisions']['extra_prefix_p50']
        assert dec['window_rows_mean'][cell] == by[cell]['window_rows']['mean']
        assert dec['predicted_is_containing'][cell] == by[cell]['predicted_is_containing']['rate'] or close(
            dec['predicted_is_containing'][cell], by[cell]['predicted_is_containing']['rate'])
    assert dec['ambiguity_material'] == {
        c: (by[c]['collisions']['share'] >= 0.10 and by[c]['collisions']['extra_prefix_p50'] >= 2)
        for c in CELLS}
    assert dec['window_is_wide'] == {c: by[c]['window_rows']['mean'] >= 8 for c in CELLS}
    assert dec['encoding_refinement_justified'] == any(
        dec['ambiguity_material'][c] and dec['window_is_wide'][c] for c in CELLS)
    assert dec['justified_cells'] == [c for c in CELLS
                                      if dec['ambiguity_material'][c] and dec['window_is_wide'][c]]
    assert dec['production_adopted'] is False

    verification = {'status': 'verified', 'cells': len(audit), 'sanitized_cells': len(san),
                    'justified_cells': dec['justified_cells'],
                    'editable_sources': {p: {'current': sha(WORKSPACE / p), 'recorded_at_run': run_hashes[p]}
                                         for p in sorted(editable)},
                    'checks': ['sources', 'artifacts', 'inputs', 'counters', 'sanitized', 'decision']}
    (out / 'verification.json').write_text(json.dumps(verification, indent=2) + '\n')
    print(json.dumps(verification['status']), len(audit), 'cells', 'justified', dec['justified_cells'])


if __name__ == '__main__':
    main()

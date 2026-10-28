"""Close C2 from frozen observations; do not rerun or rewrite the measurement."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE = Path(__file__).resolve().parents[2]
ARMS = {'region', 'window', 'window_AA', 'full', 'full_AA', 'directory', 'ART', 'HOT'}


def read(path):
    return json.loads(path.read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('out', type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    identity, raw, ledger, sanitized, recorded = (
        read(out / name) for name in ('identity.json', 'raw.json', 'ledger.json',
                                     'ledger_sanitized.json', 'summary.json'))
    drift = [path for path, digest in identity['sources'].items()
             if sha(WORKSPACE / path) != digest]
    # This pre-existing verifier was edited after the run. Disclose the mismatch;
    # no measured C++ source, input, binary, or original result may drift.
    assert drift == ['07_Eval_Integration/diagnostics/verify_shortkey_entry.py'], drift
    for path, digest in identity['artifacts'].items():
        assert sha(out / path) == digest, path
    e2 = WORKSPACE / '07_Eval_Integration/results/e2_generalization_20261018'
    for path, digest in identity['reused_libraries'].items():
        assert sha(e2 / path) == digest, path
    for item in identity['inputs'].values():
        for kind in ('rows', 'queries'):
            assert sha(WORKSPACE / item[kind + '_path']) == item[kind + '_sha256']
    archive = read(out / 'log_archive.json')
    assert sha(out / 'commands.log.gz') == archive['gzip_sha256']
    assert hashlib.sha256(gzip.decompress((out / 'commands.log.gz').read_bytes())).hexdigest() == archive['raw_sha256']
    assert len(raw) == 960 and len(recorded) == 4
    cells = []
    for old in recorded:
        cell = old['cell']
        rows = [r for r in raw if r['cell'] == cell]
        assert len(rows) == 240
        paired = []
        for process in range(5):
            block = [r for r in rows if r['process'] == process]
            assert len(block) == 48
            table = {}
            for repeat in range(6):
                seq = sorted((r for r in block if r['repeat'] == repeat), key=lambda r: r['position'])
                assert [r['position'] for r in seq] == list(range(8))
                assert {r['arm'] for r in seq} == ARMS
                assert all(r['operations'] == identity['inputs'][cell]['queries'] and r['elapsed_ns'] > 0 for r in seq)
                assert len({r['checksum'] for r in seq if r['arm'] in ('full', 'full_AA', 'directory', 'ART', 'HOT')}) == 1
                assert len({r['checksum'] for r in seq if r['arm'] in ('window', 'window_AA')}) == 1
                table.update({(repeat, r['arm']): r['elapsed_ns'] for r in seq})
            paired.append(table)
        ratios = {arm: [statistics.median(t[r, arm] / t[r, 'full'] for r in range(6)) for t in paired]
                  for arm in ARMS - {'full'}}
        for arm, values in ratios.items():
            assert values == old['ratio_to_full'][arm + '/full']['process_medians']
        window_aa = [statistics.median(t[r, 'window_AA'] / t[r, 'window'] for r in range(6)) for t in paired]
        band = max(max(x, 1 / x) for x in ratios['full_AA'] + window_aa)
        assert band == old['observed_band']
        # These are paired differences between distinct executable arms, not a
        # causal decomposition or a proof that any stage can be removed.
        shares = {
            'region': [statistics.median(t[r, 'region'] / t[r, 'full'] for r in range(6)) for t in paired],
            'window_minus_region': [statistics.median((t[r, 'window'] - t[r, 'region']) / t[r, 'full'] for r in range(6)) for t in paired],
            'full_minus_window': [statistics.median((t[r, 'full'] - t[r, 'window']) / t[r, 'full'] for r in range(6)) for t in paired],
        }
        directory = statistics.median(ratios['directory'])
        cells.append({'cell': cell, 'observed_band': band,
                      'phase_fractions': {k: {'process_medians': v, 'median': statistics.median(v)} for k, v in shares.items()},
                      'ratio_to_full': {k: statistics.median(v) for k, v in ratios.items()},
                      'directory_10pct_gate': directory <= .90 and sum(x < 1 for x in ratios['directory']) >= 4 and 1 / directory > band})
    assert len(ledger) == 5 and len(sanitized) == 2
    for row in ledger:
        inputs = identity['inputs'][row['cell']]
        assert row['checked_keys'] == inputs['rows'] + inputs['queries']
        assert row['locations'] == row['windows'] == row['checked_keys']
        assert row['queries'] == inputs['queries'] and row['certificate_probes'] > 0
        for name, stats in row['counter'].items():
            assert abs(stats['mean'] - stats['total'] / row['queries']) <= 1e-5 * max(1, abs(stats['mean'])), name
            assert stats['median'] <= stats['p95'] <= stats['max']
    for row in sanitized:
        normal = next(r for r in ledger if r['cell'] == row['cell'])
        assert row['counter'] == normal['counter']
        assert row['checked_keys'] == normal['checked_keys']
    result = {
        'auditor_sha256': sha(Path(__file__)),
        'status': 'measurement data independently checked; original verifier source drift disclosed',
        'timing_rows': len(raw), 'ledger_cells': len(ledger), 'sanitized_cells': len(sanitized),
        'source_drift': drift, 'cells': cells,
        'original_decision_errors': [
            'tranco64_nav_share was 1-region/full: this is the complement of navigation, not navigation',
            'a fraction below 1 was compared to an A/A multiplier at least 1; that test cannot pass',
            'original exact-positioning gate used 5% rather than the registered 10%',
            'specification says region_AA; actual run has window_AA and full_AA',
            'phase arms have different return/checksum and compiler paths; subtraction is not causal timing',
            'memcmp byte counters are logical first-difference lengths, not physical bytes read',
        ],
        'decision': {
            'candidate_built': False, 'production_adopted': False,
            'certified_work_removal_demonstrated': False,
            'short_range_directory_gate': next(c for c in cells if c['cell'] == 'tranco64_norepl')['directory_10pct_gate'],
            'c2_status': 'closed: no certified work-removal candidate established',
            'next_stage': 'F1 local-page update semantics and bounded maintenance',
            'limit': 'Does not prove all short-key optimizations impossible, or establish D1/ART ranking from this diagnostic copy.',
        },
    }
    (out / 'audit_v2.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('status', 'timing_rows', 'ledger_cells', 'sanitized_cells', 'source_drift')}))
    for cell in cells:
        print(cell['cell'], {k: round(v['median'], 3) for k, v in cell['phase_fractions'].items()},
              'directory/full', round(cell['ratio_to_full']['directory'], 3), 'band', round(cell['observed_band'], 3))


if __name__ == '__main__':
    main()

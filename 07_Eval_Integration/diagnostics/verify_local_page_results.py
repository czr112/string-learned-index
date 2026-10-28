"""Recheck F1 identities/semantic records and F2 paired ratios independently."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

WORKSPACE = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def log_records(out):
    return [json.loads(line) for line in (out / 'commands.log').read_text(encoding='utf-8').splitlines() if line.startswith('{')]


def check_identity(out):
    identity = read(out / 'identity.json')
    for path, digest in identity['sources'].items():
        assert sha(WORKSPACE / path) == digest, ('source', path)
    for path, digest in identity['artifacts'].items():
        assert sha(out / path) == digest, ('artifact', path)
    for cell, item in identity['inputs'].items():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256'], cell
        if 'queries' in item:
            assert sha(WORKSPACE / item['queries_path']) == item['queries']['sha256'], cell
    return identity


def emit(out, data):
    data.update(verifier_sha256=sha(Path(__file__)), commands_log_sha256=sha(out / 'commands.log'))
    (out / 'independent_verification.json').write_text(json.dumps(data, indent=2) + '\n')
    print(out.name, json.dumps(data))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('f1', type=Path)
    parser.add_argument('--f2', type=Path)
    args = parser.parse_args()
    f1 = args.f1.resolve()
    identity = check_identity(f1)
    gates = read(f1 / 'gates.json')
    assert gates['normal'] == gates['sanitized'] and len(gates['normal']) == 4
    assert log_records(f1) == [record for build in gates.values() for records in build.values() for record in records]
    checks, operations = 0, 0
    for name, records in gates['normal'].items():
        assert records[-1]['phase'] == 'complete' and records[-1]['status'] == 'passed'
        checks += records[-1]['checks']; operations += records[-1]['operations']
        record = records[0]
        assert 1 <= record['max_source_pages'] <= 2 and record['max_rebuild_rows'] <= 2049
        if name == 'synthetic':
            assert record['failed_rebuilds'] == 3 and record['splits'] > 0 and record['coalesces'] > 0
        else:
            assert record['input_rows'] == identity['inputs'][name]['rows']
            assert record['dirty_delta_bytes'] < .1 * record['dirty_base_bytes']
    assert checks == sum(read(f1 / 'verification.json')['checks_per_build'].values())
    emit(f1, {'status': 'verified', 'checks_per_build': checks, 'recorded_operations_per_build': operations,
              'builds': 2, 'gates_per_build': 4})
    if args.f2 is None:
        return
    f2 = args.f2.resolve()
    f2_identity = check_identity(f2)
    assert f2_identity['f1_identity_sha256'] == sha(f1 / 'identity.json')
    raw, summary = read(f2 / 'raw.json'), read(f2 / 'summary.json')
    preflight = read(f2 / 'gates.json')
    assert len(raw) == 960 and len(summary) == len(preflight) == 6
    logged = log_records(f2)
    assert [r for r in logged if r['phase'] == 'pass'] == [
        {k: v for k, v in row.items() if k not in ('cell', 'process')} for row in raw]
    expected_gates = [{k: v for k, v in gate.items() if k != 'cell'} for gate in preflight]
    assert [r for r in logged if r['phase'] == 'gate'] == (
        [gate for gate in expected_gates for _ in range(2)] +
        [gate for gate in expected_gates for _ in range(5)])
    failed = []
    for recorded in summary:
        cell = recorded['cell']; rows = [r for r in raw if r['cell'] == cell]
        expected = next(r for r in preflight if r['cell'] == cell)
        assert len(rows) == 160
        assert all(r['operations'] == expected['queries'] for r in rows)
        per_process, aa = [], []
        for process in range(5):
            block = [r for r in rows if r['process'] == process]
            assert len(block) == 32
            paired = {}
            for repeat in range(8):
                seq = sorted((r for r in block if r['repeat'] == repeat), key=lambda r: r['position'])
                assert [r['position'] for r in seq] == list(range(4))
                assert {r['arm'] for r in seq} == {'local', 'local_AA', 'snapshot', 'snapshot_AA'}
                assert len({r['checksum'] for r in seq}) == 1
                assert all(r['elapsed_ns'] > 0 for r in seq)
                for r in seq:
                    paired[repeat, r['arm']] = r['elapsed_ns']
            per_process.append(statistics.median(paired[r, 'local'] / paired[r, 'snapshot'] for r in range(8)))
            for arm in ('local', 'snapshot'):
                aa.append(statistics.median(paired[r, arm + '_AA'] / paired[r, arm] for r in range(8)))
        ratio = statistics.median(per_process)
        band = max(max(value, 1 / value) for value in aa)
        assert recorded['process_medians'] == per_process
        assert recorded['local_over_snapshot'] == ratio and recorded['aa_band'] == band
        failure = ratio > 1.15 and sum(value > 1 for value in per_process) >= 4 and ratio > band
        assert recorded['read_budget_failed'] == failure
        for arm, value in recorded['ns_per_op'].items():
            assert value == statistics.median(r['elapsed_ns'] / r['operations'] for r in rows if r['arm'] == arm)
        if failure:
            failed.append(cell)
    decision = read(f2 / 'decision.json')
    assert decision['read_budget_failed_cells'] == failed
    assert decision['production_adopted'] is False and decision['full_read_write_matrix_started'] is False
    emit(f2, {'status': 'verified', 'timing_rows': len(raw), 'cells': len(summary),
              'read_budget_failed_cells': failed})


if __name__ == '__main__':
    main()

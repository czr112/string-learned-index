"""F2 necessary read-budget screen, after the F1 semantic gate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

WORKSPACE = Path(__file__).resolve().parents[2]
ROOT = WORKSPACE / '07_Eval_Integration'
ARMS = {'local', 'local_AA', 'snapshot', 'snapshot_AA'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(raw):
    summary = []
    for cell in sorted({r['cell'] for r in raw}):
        block = [r for r in raw if r['cell'] == cell]
        assert len(block) == 160
        ratios, aa = [], []
        for process in range(5):
            rows = [r for r in block if r['process'] == process]
            assert len(rows) == 32
            table = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows}
            assert len(table) == 32
            for repeat in range(8):
                seq = sorted((r for r in rows if r['repeat'] == repeat), key=lambda r: r['position'])
                assert {r['arm'] for r in seq} == ARMS and [r['position'] for r in seq] == list(range(4))
                assert len({r['checksum'] for r in seq}) == 1
                assert all(r['elapsed_ns'] > 0 and r['operations'] > 0 for r in seq)
            ratios.append(statistics.median(table[r, 'local'] / table[r, 'snapshot'] for r in range(8)))
            for original in ('local', 'snapshot'):
                aa.append(statistics.median(table[r, original + '_AA'] / table[r, original] for r in range(8)))
        band = max(max(x, 1 / x) for x in aa)
        median = statistics.median(ratios)
        summary.append({'cell': cell, 'local_over_snapshot': median, 'process_medians': ratios,
                        'slower_processes': sum(x > 1 for x in ratios), 'aa_band': band,
                        'read_budget_failed': median > 1.15 and sum(x > 1 for x in ratios) >= 4 and median > band,
                        'ns_per_op': {arm: statistics.median(r['elapsed_ns'] / r['operations'] for r in block if r['arm'] == arm)
                                      for arm in ARMS}})
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='f2_local_page_read_20261018')
    args = parser.parse_args()
    assert Path(args.output).name == args.output
    f1 = ROOT / 'results/f1_local_page_20261018b'
    assert json.loads((f1 / 'verification.json').read_text())['status'] == 'passed'
    old = json.loads((f1 / 'identity.json').read_text())
    for path, digest in old['sources'].items():
        assert sha(WORKSPACE / path) == digest
    e2 = json.loads((ROOT / 'results/e2_generalization_20261018/identity.json').read_text())
    inputs = {cell: e2['inputs'][cell] for cell in ('tranco64_norepl', 'mixed_full_norepl', 'skew_full_norepl')}
    for item in inputs.values():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries']['sha256']
    sources = {path: digest for path, digest in old['sources'].items() if not path.endswith(('run_local_page.py', 'check_local_page.cpp'))}
    for path in (Path(__file__), ROOT / 'diagnostics/bench_local_page_read.cpp', ROOT / 'diagnostics/F2_READ_ENTRY_SPEC_20261018.md'):
        sources[str(path.relative_to(WORKSPACE))] = sha(path)
    out = ROOT / 'results' / args.output
    out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    identity = {'sources': sources, 'inputs': inputs, 'commands': [], 'artifacts': {},
                'cpu': min(os.sched_getaffinity(0)), 'f1_identity_sha256': sha(f1 / 'identity.json'),
                'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0]}
    def save(name, value):
        (out / name).write_text(json.dumps(value, indent=2) + '\n')
    save('identity.json', identity)
    log = (out / 'commands.log').open('x')
    def run(command, env=None):
        command = list(map(str, command)); identity['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        result = subprocess.run(command, cwd=WORKSPACE, capture_output=True, text=True, timeout=600, env=env)
        log.write(result.stdout + result.stderr); log.flush()
        if result.returncode:
            save('failure.json', {'command': command, 'rc': result.returncode, 'stderr': result.stderr})
            save('identity.json', identity)
            raise RuntimeError('F2 read screen failed; see failure.json')
        assert not result.stderr
        return result.stdout
    flags = ['-std=c++17', '-march=x86-64-v3', '-I' + str(WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(ROOT / 'learned-index/src')]
    driver = build / 'read_budget'
    san = build / 'read_budget_san'
    for exe, opts in ((driver, ['-O3']), (san, ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])):
        print('compiling', exe.name, flush=True)
        run(['g++'] + flags + opts + [ROOT / 'diagnostics/bench_local_page_read.cpp',
                                     ROOT / 'learned-index/src/unified/runner.cpp', '-o', exe])
        identity['artifacts'][str(exe.relative_to(out))] = sha(exe)
    gates, raw = [], []
    def command(exe, item, state, process, mode):
        return ['taskset', '-c', identity['cpu'], exe, WORKSPACE / item['rows_path'],
                WORKSPACE / item['queries_path'], state, process, mode]
    for name, item in inputs.items():
        for state in ('clean', 'dirty'):
            cell = name + '/' + state
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            normal = json.loads(run(command(driver, item, state, 0, 'gate')))
            sanitized = json.loads(run(command(san, item, state, 0, 'gate'), env))
            assert normal == sanitized
            gates.append({'cell': cell, **normal}); save('gates.json', gates)
            print('sanitized gate', cell, normal['checks'], flush=True)
    for name, item in inputs.items():
        for state in ('clean', 'dirty'):
            cell = name + '/' + state
            expected_gate = next(g for g in gates if g['cell'] == cell)
            for process in range(5):
                lines = [json.loads(line) for line in run(command(driver, item, state, process, 'timing')).splitlines()]
                gate, passes = lines[0], lines[1:]
                assert gate == {k: v for k, v in expected_gate.items() if k != 'cell'}
                assert len(passes) == 32
                for row in passes:
                    assert row['operations'] == item['queries']['count']
                    row.update(cell=cell, process=process)
                raw.extend(passes); save('raw.json', raw)
            print('paired reads', cell, 'x5 complete', flush=True)
    assert len(raw) == 960
    summary = summarize(raw); save('summary.json', summary)
    failed = [c['cell'] for c in summary if c['read_budget_failed']]
    save('decision.json', {'read_budget_failed_cells': failed, 'full_read_write_matrix_started': False,
                           'production_adopted': False,
                           'status': 'read budget failed; close this bridge before full matrix' if failed else 'read screen not rejected; full dynamic comparison remains pending'})
    for path, digest in sources.items():
        assert sha(WORKSPACE / path) == digest
    for item in inputs.values():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries']['sha256']
    save('identity.json', identity); log.close()
    for cell in summary:
        print(cell['cell'], round(cell['local_over_snapshot'], 3), 'band', round(cell['aa_band'], 3), 'failed', cell['read_budget_failed'])


if __name__ == '__main__':
    main()

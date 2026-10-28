"""Frozen stage-3 entry, one-level original BlockModel refinement; counts only."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess

from generate_bm_local_refine import rendered

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
DIAG = ROOT / 'diagnostics'
MODEL = ROOT / 'upstream_snapshots/model_e8e05411'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
CELLS = ['english_words', 'memetracker_urls', 'memetracker_phrases', 'tranco64_norepl']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text(encoding='utf-8'))
def save(p, x): p.write_text(json.dumps(x, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def decide(records):
    by = {r['cell']: r for r in records}
    reductions = {c: 1 - by[c]['refined']['fence_compares'] / by[c]['current']['fence_compares'] for c in CELLS}
    shares = {c: by[c]['memory']['added'] / by[c]['memory']['baseline'] for c in CELLS}
    tags = all(by[c]['refined']['blocks'] <= by[c]['current']['blocks'] for c in CELLS)
    keys = all(by[c]['refined']['key_compares'] <= by[c]['current']['key_compares'] for c in CELLS)
    enough = reductions['memetracker_urls'] >= 0.25
    space = all(x <= 0.05 for x in shares.values())
    passed = enough and tags and keys and space
    return {'fence_reduction': reductions, 'metadata_added_over_baseline': shares,
            'main_cell': 'memetracker_urls', 'main_25_percent': enough,
            'blocks_never_increase': tags, 'key_verification_never_increases': keys,
            'metadata_within_5_percent': space, 'entry_passes': passed,
            'candidate_closed': not passed,
            'timing_requires_separate_registration': True, 'timing_performed': False,
            'production_adopted': False, 'default_changed': False,
            'status': 'entry only, counts are not timings'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='bm_local_refine_entry_20261019')
    args = parser.parse_args()
    assert Path(args.output).name == args.output
    assert (DIAG / 'bm_local_refine_page.hpp').read_text(encoding='utf-8') == rendered()
    real = read(ROOT / 'results/bm_real_20261018/identity.json')
    e2 = read(ROOT / 'results/e2_generalization_20261018/identity.json')
    lock = read(ROOT / 'results/upstream_update_audit_20261018/identity.json')
    for name, path in [('model', MODEL), ('benchmark', BENCH)]:
        assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=path, text=True).strip() == lock['repositories'][name]['commit']
    inputs = {}
    for cell in CELLS:
        item = real['inputs'][cell] if cell != 'tranco64_norepl' else e2['inputs'][cell]
        inputs[cell] = {'rows_path': item['rows_path'], 'rows_sha256': item['rows_sha256'],
                        'rows': item['rows'], 'queries_path': item['queries_path'],
                        'queries_sha256': item['queries']['sha256'], 'queries': item['queries']['count']}
    for item in inputs.values():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    out = ROOT / 'results' / args.output
    out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3',
             '-I' + str(WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'), '-I' + str(DIAG)]
    common = [DIAG / 'audit_bm_local_refine.cpp', MODEL / 'src/block_model.cpp', BENCH / 'src/unified/runner.cpp']
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'inputs': inputs, 'sources': {}, 'artifacts': {}, 'commands': [],
            'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': lock['repositories']['model']['commit'],
            'benchmark_commit': lock['repositories']['benchmark']['commit'],
            'protocol': 'one-level original BM; four cells and all four sanitized; counts only',
            'limits': ['root navigation outside counters', 'no timing', 'C++ new calls only, not a complete heap profiler',
                       'capacity ledger, not allocator heap/RSS', 'read-only candidate; no dynamic performance']}
    log = (out / 'commands.log').open('x', encoding='utf-8')

    def run(command, env=None):
        command = list(map(str, command)); meta['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, env=env, timeout=3600)
        log.write(p.stdout); log.write(p.stderr); log.flush()
        if p.returncode:
            save(out / 'failure.json', {'command': command, 'rc': p.returncode, 'stderr': p.stderr[-12000:]})
            save(out / 'identity.json', meta)
            raise RuntimeError('command failed; original failure retained')
        return p.stdout

    deps = run(['g++'] + flags + ['-MM'] + common)
    paths = {Path(__file__), DIAG / 'verify_bm_local_refine.py', DIAG / 'generate_bm_local_refine.py',
             DIAG / 'BM_LOCAL_REFINE_SPEC_20261019.md'}
    for line in deps.replace('\\\n', ' ').splitlines():
        if ':' in line:
            for name in line.split(':', 1)[1].split(): paths.add(Path(name).resolve())
    for path in sorted(paths):
        rel = str(path.relative_to(WORKSPACE)); meta['sources'][rel] = sha(path)
        archived = out / 'frozen_sources' / rel
        archived.parent.mkdir(parents=True, exist_ok=True); archived.write_bytes(path.read_bytes())
    save(out / 'identity.json', meta)
    for name, opts in [('audit', ['-O2']), ('audit_san', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
        print('building', name, flush=True)
        exe = build / name
        run(['g++'] + flags + opts + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        save(out / 'identity.json', meta)
    normal, sanitized = [], []
    for flavor, records in [('audit', normal), ('audit_san', sanitized)]:
        for cell in CELLS:
            item = inputs[cell]
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                       UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1') if flavor.endswith('_san') else None
            raw = run(['taskset', '-c', meta['cpu'], build / flavor,
                       WORKSPACE / item['rows_path'], WORKSPACE / item['queries_path']], env=env)
            record = json.loads(raw); record['cell'] = cell
            assert record['mismatch'] == record['query_new_calls'] == 0
            assert record['rows'] == item['rows'] and record['queries'] == item['queries']
            if flavor.endswith('_san'):
                assert record == normal[CELLS.index(cell)], ('sanitized count drift', cell)
            records.append(record)
            save(out / ('counts_sanitized.json' if flavor.endswith('_san') else 'counts.json'), records)
            print(flavor, cell, 'fence', record['current']['fence_compares'], '->', record['refined']['fence_compares'],
                  'families', record['families'], 'metadata', round(record['memory']['added']/record['memory']['baseline'], 5), flush=True)
    for path, digest in meta['sources'].items(): assert sha(WORKSPACE / path) == digest, ('source drift', path)
    for item in inputs.values():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
        assert sha(WORKSPACE / item['queries_path']) == item['queries_sha256']
    save(out / 'decision.json', decide(normal)); save(out / 'identity.json', meta)
    log.close(); raw = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as gz: gz.write(raw)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == raw
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(raw).hexdigest(), 'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print(json.dumps(decide(normal), ensure_ascii=False), flush=True)


if __name__ == '__main__': main()

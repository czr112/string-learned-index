"""Stage-5 entry: replay the frozen traces, check every operation, record first observations."""
import argparse
import gzip
import hashlib
import json
import os
import statistics
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
TRACES = ROOT / 'results/bm_dynamic_inputs_20261019'
CELLS = ['english_words', 'english_words_80_20', 'memetracker_urls', 'memetracker_urls_80_20']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text(encoding='utf-8'))
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_dynamic_entry_20261019')
    a = p.parse_args(); assert Path(a.output).name == a.output
    traces = read(TRACES / 'identity.json')
    for cell, item in traces['traces'].items():
        assert sha(WS / item['rows_path']) == item['rows_sha256'], cell
        assert sha(WS / item['operations_path']) == item['operations_sha256'], cell
    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    sources = {str(x.relative_to(WS)): sha(x) for x in sorted({
        Path(__file__), ROOT / 'diagnostics/verify_bm_dynamic_entry.py',
        ROOT / 'diagnostics/replay_bm_dynamic.cpp', ROOT / 'diagnostics/prepare_bm_dynamic.py',
        ROOT / 'diagnostics/bm_bounded_index.hpp', ROOT / 'diagnostics/bm_page_index.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
        TRACES / 'identity.json',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        WS / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp',
        BENCH / 'scripts/benchmark_lib/workload.py', BENCH / 'scripts/benchmark_lib/interchange.py'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'sources': sources, 'traces': traces['traces'],
            'artifacts': {}, 'commands': [],
            'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip(),
            'benchmark_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=BENCH, text=True).strip(),
            'protocol': 'one replay per frozen trace; every operation checked against std::map; '
                        'final map verified by full read and full ordered scan; single process, no pairing',
            'limits': ['entry only: no paired timing, no throughput claim',
                       'uniform insert targets from the upstream generator; no concentrated writes',
                       'update/erase weights are zero in these two ratios',
                       'HOT absent; no read-budget comparison against a rebuilt snapshot yet']}
    log = (out / 'commands.log').open('x', encoding='utf-8')

    def run(command, env=None):
        command = list(map(str, command)); meta['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        r = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=1800, env=env)
        log.write(r.stdout); log.write(r.stderr); log.flush()
        if r.returncode:
            save(out / 'failure.json', {'command': command, 'rc': r.returncode, 'stderr': r.stderr[-12000:]})
            save(out / 'identity.json', meta); raise RuntimeError('command failed; see failure.json')
        return r

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3',
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'), '-I' + str(ROOT / 'diagnostics')]
    common = [ROOT / 'diagnostics/replay_bm_dynamic.cpp', MODEL / 'src/block_model.cpp']

    def build_binary(name, options):
        exe = build / name
        run(['g++'] + flags + options + common + ['-o', exe])
        meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
        return exe

    print('building dynamic replayer', flush=True)
    driver = build_binary('replay_bm_dynamic', ['-O3'])
    sanitized = build_binary('replay_bm_dynamic_san',
                             ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    save(out / 'identity.json', meta)

    records = []
    for cell in CELLS:
        item = traces['traces'][cell]
        r = run(['taskset', '-c', meta['cpu'], driver, WS / item['rows_path'], WS / item['operations_path']])
        record = json.loads(r.stdout); record['cell'] = cell
        assert record['mismatches'] == 0
        assert record['find_ops'] == item['kinds'].get('1', 0)
        assert record['insert_ops'] == item['kinds'].get('2', 0)
        assert record['initial_rows'] == item['initial_rows']
        assert record['final_read_checked'] == record['final_size']
        assert record['final_scan_rows'] == record['final_size']
        records.append(record); save(out / 'records.json', records)
        print('replay', cell, 'mismatches 0, final', record['final_size'], flush=True)
    san = []
    for cell in ['english_words', 'memetracker_urls_80_20']:
        item = traces['traces'][cell]
        r = run(['taskset', '-c', meta['cpu'], sanitized, WS / item['rows_path'], WS / item['operations_path']],
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                         UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        record = json.loads(r.stdout); record['cell'] = cell
        normal = next(x for x in records if x['cell'] == cell)
        for field in ('initial_rows', 'operations', 'find_ops', 'insert_ops', 'final_size', 'mismatches',
                      'root_publications', 'rebuilds', 'rows_rebuilt', 'inplace_inserts'):
            assert record[field] == normal[field], (cell, field)
        san.append(record); save(out / 'records_sanitized.json', san)
        print('sanitized', cell, 'matches', flush=True)

    decision = {'cells': {r['cell']: {'find_ops': r['find_ops'], 'insert_ops': r['insert_ops'],
                                      'insert_new': r['insert_new'], 'insert_dup': r['insert_dup'],
                                      'rebuilds': r['rebuilds'], 'inplace_inserts': r['inplace_inserts'],
                                      'root_publications': r['root_publications'],
                                      'rows_rebuilt': r['rows_rebuilt'],
                                      'final_size': r['final_size'],
                                      'find_ns_per_op': r['find_ns'] / max(1, r['find_ops']),
                                      'write_ns_per_op': r['write_ns'] / max(1, r['insert_ops'])}
                           for r in records},
                'entry_passed': all(r['mismatches'] == 0 for r in records) and len(san) == 2,
                'paired_timing_done': False, 'throughput_claim': 'none',
                'read_budget_vs_snapshot': 'not measured yet',
                'production_adopted': False,
                'status': 'stage-5 entry: correctness of frozen traces; single-process observations only'}
    save(out / 'decision.json', decision)
    for cell, item in meta['traces'].items():
        assert sha(WS / item['rows_path']) == item['rows_sha256']
        assert sha(WS / item['operations_path']) == item['operations_sha256']
    for path, digest in sources.items(): assert sha(WS / path) == digest, path
    save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    assert gzip.decompress((out / 'commands.log.gz').read_bytes()) == data
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(records), 'traces', len(san), 'sanitized', flush=True)


if __name__ == '__main__':
    main()

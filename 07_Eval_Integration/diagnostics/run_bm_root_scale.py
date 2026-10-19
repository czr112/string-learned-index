"""Stage-5 root cost: fixed insert batch across page counts."""
import argparse
import gzip
import hashlib
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
CASES = [('tranco64', '07_Eval_Integration/results/static_interface_20261012c/tranco_64k_rows.bin', 32),
         ('words100k', '07_Eval_Integration/results/bm_space_gen_inputs_20261019/interchange/english_words_100000_s20261019_rows.bin', 49),
         ('skew131k', '07_Eval_Integration/results/matrix2_skew/interchange/rows.bin', 64),
         ('tranco262k', '07_Eval_Integration/results/matrix2_tranco/interchange/rows.bin', 128)]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_root_scale_20261019')
    a = p.parse_args(); assert Path(a.output).name == a.output
    inputs = {}
    for label, rel, pages in CASES:
        path = WS / rel
        assert path.is_file(), rel
        inputs[label] = {'rows_path': rel, 'rows_sha256': sha(path), 'expected_pages': pages}
    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    sources = {str(x.relative_to(WS)): sha(x) for x in sorted({
        Path(__file__), ROOT / 'diagnostics/bench_bm_root_scale.cpp',
        ROOT / 'diagnostics/BM_ROOT_SCALE_SPEC_20261019.md',
        ROOT / 'diagnostics/bm_bounded_index.hpp', ROOT / 'diagnostics/bm_page_index.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        WS / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'inputs': inputs, 'sources': sources, 'artifacts': {},
            'commands': [], 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'upstream_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=MODEL, text=True).strip(),
            'protocol': 'one process per page count; 1,000 synthetic inserts spread evenly over the row set; '
                        'root ledger taken as the delta of the maintenance counters',
            'limits': ['single process, unpaired', 'evenly spread inserts make every page split once',
                       'the batch is synthetic growth, not a mixed workload', 'no adoption decision']}
    log = (out / 'commands.log').open('x', encoding='utf-8')

    def run(command):
        command = list(map(str, command)); meta['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        r = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=3600)
        log.write(r.stdout); log.write(r.stderr); log.flush()
        if r.returncode:
            save(out / 'failure.json', {'command': command, 'rc': r.returncode, 'stderr': r.stderr[-12000:]})
            save(out / 'identity.json', meta); raise RuntimeError('command failed')
        return r

    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3',
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'), '-I' + str(ROOT / 'diagnostics')]
    exe = build / 'rootscale'
    run(['g++'] + flags + ['-O2', ROOT / 'diagnostics/bench_bm_root_scale.cpp',
                           MODEL / 'src/block_model.cpp', '-o', exe])
    meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
    save(out / 'identity.json', meta)

    records = []
    for label, rel, pages in CASES:
        r = run(['taskset', '-c', meta['cpu'], exe, WS / rel, '1000', label])
        record = json.loads(r.stdout)
        assert record['mismatches'] == 0 and record['inserted'] == record['inserts']
        assert record['pages'] == pages, (label, record['pages'], pages)
        records.append(record); save(out / 'records.json', records)
        print(label, 'pages', record['pages'], '->', record['pages_after'],
              'pubs', record['publications'], 'entries', record['entries_copied'],
              'per-pub', round(record['entries_per_publication'], 1), flush=True)

    # Linear-with-pages check: entries per publication should sit between pages and 2*pages
    # because the root is copied after the split has already added a page.
    linear = {}
    for record in records:
        pages = record['pages']
        ratio = record['entries_per_publication'] / pages
        linear[record['label']] = {'pages': pages, 'entries_per_publication': record['entries_per_publication'],
                                   'ratio_to_pages': ratio,
                                   'within_expected_band': 1.0 <= ratio <= 2.0}
    decision = {'root_work': linear,
                'root_work_grows_with_pages': all(v['within_expected_band'] for v in linear.values()),
                'total_entries_copied': {r['label']: r['entries_copied'] for r in records},
                'publications': {r['label']: r['publications'] for r in records},
                'p99_ns': {r['label']: r['pause_ns']['p99'] for r in records},
                'path_copy_justified': any(r['entries_copied'] > 4 * 2048 for r in records),
                'production_adopted': False,
                'status': 'stage-5 root cost scaling; synthetic growth batch, unpaired'}
    save(out / 'decision.json', decision)
    for label, item in meta['inputs'].items():
        assert sha(WS / item['rows_path']) == item['rows_sha256']
    for path, digest in sources.items(): assert sha(WS / path) == digest, path
    save(out / 'identity.json', meta)
    save(out / 'verification.json', {'status': 'verified',
                                     'checks': ['sources', 'inputs', 'pages', 'zero mismatches', 'linear band'],
                                     'cells': len(records)})
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(records), 'page counts', flush=True)


if __name__ == '__main__':
    main()

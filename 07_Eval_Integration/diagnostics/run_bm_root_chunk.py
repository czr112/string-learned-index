"""Stage-5 root entry: whole-root copy versus chunked COW publication."""
import argparse
import gzip
import hashlib
import json
import os
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WS = ROOT.parent
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
CELLS = {
    'tranco64': ('07_Eval_Integration/results/static_interface_20261012c/tranco_64k_rows.bin',
                 '07_Eval_Integration/results/e2_generalization_20261018/tranco64_norepl_queries.bin'),
    'skew131k': ('07_Eval_Integration/results/matrix2_skew/interchange/rows.bin',
                 '07_Eval_Integration/results/e2_generalization_20261018/skew_full_norepl_queries.bin'),
    'tranco262k': ('07_Eval_Integration/results/matrix2_tranco/interchange/rows.bin',
                   '07_Eval_Integration/results/e2_generalization_20261018/tranco64_norepl_queries.bin'),
}
ARMS = ['flat', 'chunked', 'flat_AA', 'chunked_AA']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def save(p, obj): p.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n')


def main():
    p = argparse.ArgumentParser(); p.add_argument('--output', default='bm_root_chunk_20261019')
    a = p.parse_args(); assert Path(a.output).name == a.output
    inputs = {}
    for label, (rows, queries) in CELLS.items():
        inputs[label] = {'rows_path': rows, 'rows_sha256': sha(WS / rows),
                         'queries_path': queries, 'queries_sha256': sha(WS / queries)}
    out = ROOT / 'results' / a.output; out.mkdir(exist_ok=False)
    build = out / 'build'; build.mkdir()
    sources = {str(x.relative_to(WS)): sha(x) for x in sorted({
        Path(__file__), ROOT / 'diagnostics/verify_bm_root_chunk.py',
        ROOT / 'diagnostics/bench_bm_root_chunk.cpp', ROOT / 'diagnostics/BM_ROOT_CHUNK_SPEC_20261019.md',
        ROOT / 'diagnostics/bm_page_index.hpp', ROOT / 'diagnostics/bm_page_root.hpp',
        ROOT / 'diagnostics/bm_page_core.hpp', ROOT / 'diagnostics/static_input.hpp',
        MODEL / 'src/block_model.cpp', MODEL / 'src/block_model.h', MODEL / 'src/key_bytes.h',
        WS / '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp'})}
    meta = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'cpu': min(os.sched_getaffinity(0)), 'inputs': inputs, 'sources': sources, 'artifacts': {},
            'commands': [], 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
            'protocol': 'routing-only comparison: flat digest root versus 16-key chunked root, '
                        '5 processes x 6 Williams rows; publication work computed from the real bindings',
            'limits': ['routing only; no page or model access in the timed loop',
                       'the chunked root is a diagnostic type, not integrated into the index',
                       'publication entries estimate a copy-on-write chunk, not a measured allocation',
                       'no adoption decision']}
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
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'),
             '-I' + str(ROOT / 'learned-index/src'), '-I' + str(ROOT / 'diagnostics')]
    exe = build / 'rootchunk'
    run(['g++'] + flags + ['-O3', ROOT / 'diagnostics/bench_bm_root_chunk.cpp',
                           MODEL / 'src/block_model.cpp', BENCH / 'src/unified/runner.cpp', '-o', exe])
    meta['artifacts'][str(exe.relative_to(out))] = sha(exe)
    save(out / 'identity.json', meta)

    gates, raw = [], []
    for label, item in inputs.items():
        for process in range(5):
            r = run(['taskset', '-c', meta['cpu'], exe, WS / item['rows_path'],
                     WS / item['queries_path'], process])
            records = [json.loads(line) for line in r.stdout.splitlines()]
            gate = [x for x in records if x.get('phase') == 'gate'][0]
            passes = [x for x in records if x.get('phase') == 'pass']
            assert gate['mismatches'] == 0 and gate['checked'] > 0
            assert len(passes) == 24 and {x['arm'] for x in passes} == set(ARMS)
            for repeat in range(6):
                seq = sorted((x for x in passes if x['repeat'] == repeat), key=lambda x: x['position'])
                assert [x['position'] for x in seq] == list(range(4))
                assert len({x['checksum'] for x in seq}) == 1
            if process == 0:
                gates.append(dict(gate, cell=label)); save(out / 'gates.json', gates)
            for row in passes: row.update(cell=label, process=process)
            raw.extend(passes); save(out / 'raw.json', raw)
        print('cell', label, 'x5 done', flush=True)
    assert len(raw) == len(inputs) * 5 * 24 and len(gates) == len(inputs)

    summary = []
    for label in inputs:
        rows = [r for r in raw if r['cell'] == label]
        ratios = {}
        for name, x, y in [('chunked/flat', 'chunked', 'flat'),
                           ('flat_AA/flat', 'flat_AA', 'flat'),
                           ('chunked_AA/chunked', 'chunked_AA', 'chunked')]:
            medians = []
            for process in range(5):
                t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in rows if r['process'] == process}
                medians.append(statistics.median(t[rep, x] / t[rep, y] for rep in range(6)))
            ratios[name] = {'process_medians': medians, 'median': statistics.median(medians),
                            'faster': sum(v < 1 for v in medians), 'slower': sum(v > 1 for v in medians)}
        band = max(max(max(v, 1 / v) for v in ratios[n]['process_medians'])
                   for n in ('flat_AA/flat', 'chunked_AA/chunked'))
        gate = next(g for g in gates if g['cell'] == label)
        summary.append({'cell': label, 'ratios': ratios, 'observed_band': band,
                        'pages': gate['pages'], 'chunks': gate['chunks'],
                        'publication': gate['publication'],
                        'ns_per_op': {arm: statistics.median(r['elapsed_ns'] / r['operations']
                                                             for r in rows if r['arm'] == arm) for arm in ARMS}})
    save(out / 'summary.json', summary)
    decision = {'routing_ratio': {c['cell']: c['ratios']['chunked/flat']['median'] for c in summary},
                'publication_entry_reduction': {c['cell']: c['publication']['entry_reduction'] for c in summary},
                'publication_byte_reduction': {c['cell']: c['publication']['byte_reduction'] for c in summary},
                'routing_within_5_percent': all(abs(c['ratios']['chunked/flat']['median'] - 1) <= 0.05
                                                for c in summary),
                'publication_cut_at_least_25_percent': all(c['publication']['entry_reduction'] >= 0.25
                                                           for c in summary),
                'entry_gate_passed': all(abs(c['ratios']['chunked/flat']['median'] - 1) <= 0.05 for c in summary)
                    and all(c['publication']['entry_reduction'] >= 0.25 for c in summary),
                'integration_attempted': False,
                'production_adopted': False,
                'status': 'stage-5 root entry: routing and publication-work comparison only'}
    save(out / 'decision.json', decision)
    for label, item in inputs.items():
        assert sha(WS / item['rows_path']) == item['rows_sha256']
        assert sha(WS / item['queries_path']) == item['queries_sha256']
    for path, digest in sources.items(): assert sha(WS / path) == digest, path
    save(out / 'identity.json', meta)
    log.close(); data = (out / 'commands.log').read_bytes()
    with (out / 'commands.log.gz').open('xb') as handle:
        with gzip.GzipFile(filename='', mode='wb', fileobj=handle, mtime=0) as z: z.write(data)
    save(out / 'log_archive.json', {'raw_sha256': hashlib.sha256(data).hexdigest(),
                                    'gzip_sha256': sha(out / 'commands.log.gz')})
    (out / 'commands.log').unlink()
    print('completed', len(raw), 'timing rows', flush=True)


if __name__ == '__main__':
    main()

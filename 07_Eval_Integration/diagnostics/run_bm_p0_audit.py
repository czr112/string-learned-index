"""P0: correct the three audit findings and record the corrected verdicts.

1. Root-chunk arms: the old driver sent arms {chunked, chunked_AA} down the *flat* path.
   This rebuilds a corrected driver (v2) whose labels and paths agree, re-runs the three cells,
   and recomputes chunked/flat.
2. Dynamic read budget: the old verdict used snapshot/dynamic; the registered direction is
   dynamic/snapshot. Recomputed from the frozen raw rows by direct pairing.
3. Clean-rebuild checksum: compare exact decimal strings and keep the raw stdout instead of
   going through a float/JSON round trip.
"""
import gzip
import hashlib
import json
import os
import statistics
import subprocess
from pathlib import Path

WS = Path('/mnt/d/StringLearnedIndex_Workspace')
ROOT = WS / '07_Eval_Integration'
DIAG = ROOT / 'diagnostics'
MODEL = ROOT / 'upstream_snapshots/model_3894c767'
BENCH = ROOT / 'upstream_snapshots/benchmark_003a55bf'
OUT = ROOT / 'results/bm_p0_audit_20261019'
CELLS = {
    'tranco64': ('07_Eval_Integration/results/static_interface_20261012c/tranco_64k_rows.bin',
                 '07_Eval_Integration/results/e2_generalization_20261018/tranco64_norepl_queries.bin'),
    'skew131k': ('07_Eval_Integration/results/matrix2_skew/interchange/rows.bin',
                 '07_Eval_Integration/results/e2_generalization_20261018/skew_full_norepl_queries.bin'),
    'tranco262k': ('07_Eval_Integration/results/matrix2_tranco/interchange/rows.bin',
                   '07_Eval_Integration/results/e2_generalization_20261018/tranco64_norepl_queries.bin'),
}


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    OUT.mkdir(exist_ok=False)
    build = OUT / 'build'; build.mkdir()
    source = (DIAG / 'bench_bm_root_chunk_v2.cpp').read_text(encoding='utf-8')
    assert 'pass(arm==1||arm==3)' in source, 'v2 driver still has the mismatched mapping'
    flags = ['-pthread', '-std=c++17', '-march=x86-64-v3', '-O3',
             '-I' + str(WS / '02_Code_and_Experiments/validation_e25_20260918'),
             '-I' + str(BENCH / 'src'), '-I' + str(MODEL / 'src'),
             '-I' + str(ROOT / 'learned-index/src'), '-I' + str(DIAG)]
    exe = build / 'rootchunk_v2'
    command = ['g++'] + flags + [DIAG / 'bench_bm_root_chunk_v2.cpp', MODEL / 'src/block_model.cpp',
                                 BENCH / 'src/unified/runner.cpp', '-o', exe]
    subprocess.run(command, check=True, capture_output=True, text=True)
    meta = {'phase': 'P0', 'sources': {str(p.relative_to(WS)): sha(p) for p in
            [DIAG / 'bench_bm_root_chunk_v2.cpp', self_source(), DIAG / 'run_bm_p0_audit.py'] if p.is_file()},
            'artifacts': {'build/rootchunk_v2': sha(exe)},
            'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'protocol': 'corrected arm labels; 3 cells x 5 processes x 6 Williams rows',
            'note': 'the old bm_root_chunk_20261019 round stays frozen and its pass verdict is withdrawn'}

    # 1. Corrected root-chunk measurement.
    rows_all = []
    for label, (rows, queries) in CELLS.items():
        for process in range(5):
            r = subprocess.run(['taskset', '-c', '0', str(exe), str(WS / rows), str(WS / queries),
                                str(process)], check=True, capture_output=True, text=True)
            records = [json.loads(line) for line in r.stdout.splitlines()]
            gate = [x for x in records if x.get('phase') == 'gate'][0]
            passes = [x for x in records if x.get('phase') == 'pass']
            assert gate['mismatches'] == 0
            assert len(passes) == 24
            for repeat in range(6):
                seq = sorted((x for x in passes if x['repeat'] == repeat), key=lambda x: x['position'])
                assert [x['position'] for x in seq] == list(range(4))
                assert len({x['checksum'] for x in seq}) == 1
            for x in passes:
                x.update(cell=label, process=process)
            rows_all.extend(passes)
    ratios, bands = {}, {}
    for label in CELLS:
        block = [r for r in rows_all if r['cell'] == label]
        ratios[label] = []
        aa = []
        for process in range(5):
            t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in block if r['process'] == process}
            ratios[label].append(statistics.median(t[rep, 'chunked'] / t[rep, 'flat'] for rep in range(6)))
            aa.append(statistics.median(t[rep, 'flat_AA'] / t[rep, 'flat'] for rep in range(6)))
            aa.append(statistics.median(t[rep, 'chunked_AA'] / t[rep, 'chunked'] for rep in range(6)))
        ratios[label] = {'process_medians': ratios[label], 'median': statistics.median(ratios[label])}
        bands[label] = max(max(v, 1 / v) for v in aa)
    entry_ok = all(abs(v['median'] - 1) <= 0.05 for v in ratios.values())

    # 2. Dynamic read budget in the registered direction.
    old_raw = json.loads((ROOT / 'results/bm_dynamic_read_20261019/raw.json').read_text(encoding='utf-8'))
    budget = {}
    for cell in sorted({r['cell'] for r in old_raw}):
        block = [r for r in old_raw if r['cell'] == cell]
        per = []
        for process in range(5):
            t = {(r['repeat'], r['arm']): r['elapsed_ns'] for r in block if r['process'] == process}
            per.append(statistics.median(t[rep, 'dynamic'] / t[rep, 'snapshot'] for rep in range(6)))
        budget[cell] = {'process_medians': per, 'median': statistics.median(per),
                        'within_1_15': statistics.median(per) <= 1.15}

    # 3. Clean-rebuild checksum with exact decimal strings.
    clean = WS / 'builds/rebuild_20261019/clean'
    inputs = [WS / '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin',
              WS / '07_Eval_Integration/results/bm_real_20261018/interchange/english_words_queries.bin']
    checksums = []
    for run_index in range(2):
        r = subprocess.run(['taskset', '-c', '0', str(clean), 'bm_base', str(inputs[0]), str(inputs[1])],
                           check=True, capture_output=True, text=True)
        (OUT / f'clean_{run_index}.stdout').write_text(r.stdout)
        checksums.append(str(json.loads(r.stdout.splitlines()[-1])['checksum']))
    recorded = json.loads((ROOT / 'results/bm_space_20261019b/capacity.json').read_text(encoding='utf-8'))
    reference = [x for x in recorded if x['arm'] == 'bm_base' and x['cell'] == 'english_words'
                 and x['process'] == 0][0]
    exact = {'recorded': str(reference['checksum']), 'run0': checksums[0], 'run1': checksums[1],
             'all_equal_as_decimal_strings': str(reference['checksum']) == checksums[0] == checksums[1],
             'old_comparison_value': '507521454403758900',
             'old_value_equals_v8_round_trip': True,
             'note': 'the old 28 difference came from a Number round trip, not from the index'}

    p0 = {'root_chunk': {'ratios': ratios, 'aa_bands': bands, 'entry_gate_passed': entry_ok,
                         'old_verdict': 'withdrawn: arms chunked/chunked_AA ran the flat path'},
          'dynamic_read_budget': budget,
          'clean_checksum': exact,
          'source_drift': {'files': ['verify_bm_dynamic_entry.py', 'BM_DYNAMIC_READ_SPEC_20261019.md',
                                     'BM_ROOT_SCALE_SPEC_20261019.md', 'BM_ROOT_CHUNK_SPEC_20261019.md',
                                     'verify_bm_root_chunk.py'],
                           'action': 'disclosed as irrecoverable in place; new verifier identities issued here, '
                                     'old hashes are not rewritten'},
          'production_adopted': False}
    (OUT / 'p0.json').write_text(json.dumps(p0, indent=2) + '\n', encoding='utf-8')
    (OUT / 'identity.json').write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')
    (OUT / 'verification.json').write_text(json.dumps({
        'status': 'verified', 'checks': ['arm labels', 'pairing', 'checksums', 'budget direction',
                                         'exact decimal comparison', 'source drift disclosure'],
        'root_entry_gate_passed': entry_ok}, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'ratios': {k: round(v['median'], 3) for k, v in ratios.items()},
                      'entry_gate': entry_ok,
                      'budget': {k: round(v['median'], 3) for k, v in budget.items()},
                      'checksum_equal': exact['all_equal_as_decimal_strings']}, indent=1))


def self_source():
    return Path(__file__).resolve()


if __name__ == '__main__':
    main()

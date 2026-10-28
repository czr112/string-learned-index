"""F1 semantic/locality gate; no timing or adoption claims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

WORKSPACE = Path(__file__).resolve().parents[2]
ROOT = WORKSPACE / '07_Eval_Integration'
UPSTREAM = WORKSPACE / '02_Code_and_Experiments/validation_e25_20260918'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='f1_local_page_20261018')
    args = parser.parse_args()
    assert Path(args.output).name == args.output
    out = ROOT / 'results' / args.output
    out.mkdir(exist_ok=False)
    build = out / 'build'
    build.mkdir()
    sources = [Path(__file__), ROOT / 'diagnostics/local_page_index.hpp',
               ROOT / 'diagnostics/check_local_page.cpp', ROOT / 'diagnostics/F1_LOCAL_PAGE_SPEC_20261018.md',
               ROOT / 'diagnostics/collision_refined_index.hpp', ROOT / 'diagnostics/collision_refinement.hpp',
               UPSTREAM / 'static_physical_index.hpp', UPSTREAM / 'physical_locator_index.hpp',
               ROOT / 'diagnostics/static_input.hpp', ROOT / 'learned-index/src/unified/runner.cpp',
               ROOT / 'learned-index/src/unified/runner.h', ROOT / 'learned-index/src/index_contract.h']
    e2 = json.loads((ROOT / 'results/e2_generalization_20261018/identity.json').read_text())
    inputs = {cell: e2['inputs'][cell] for cell in ('tranco64_norepl', 'mixed_full_norepl', 'skew_full_norepl')}
    for item in inputs.values():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
    identity = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=WORKSPACE, text=True).strip(),
                'sources': {str(p.relative_to(WORKSPACE)): sha(p) for p in sources},
                'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
                'commands': [], 'artifacts': {}, 'inputs': inputs, 'compiler_diagnostics': {},
                'limits': ['semantic gate only; no performance adoption',
                           'accounted peak excludes allocator/tree-node metadata and internal D1 build scratch']}
    def save(name, obj):
        (out / name).write_text(json.dumps(obj, indent=2) + '\n')
    save('identity.json', identity)
    log = (out / 'commands.log').open('x')
    records = {}
    def run(command, env=None):
        command = list(map(str, command))
        identity['commands'].append(command)
        log.write('$ ' + ' '.join(command) + '\n'); log.flush()
        result = subprocess.run(command, cwd=WORKSPACE, capture_output=True, text=True, timeout=600, env=env)
        log.write(result.stdout + result.stderr); log.flush()
        if result.returncode:
            save('failure.json', {'command': command, 'rc': result.returncode, 'stderr': result.stderr})
            save('identity.json', identity)
            raise RuntimeError('F1 gate failed; see failure.json')
        return result
    flags = ['-std=c++17', '-march=x86-64-v3', '-Wall', '-Wextra', '-I' + str(UPSTREAM),
             '-I' + str(ROOT / 'learned-index/src')]
    for kind, options in [('normal', ['-O2']),
                          ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
        exe = build / ('check_local_page_' + kind)
        print('compiling', kind, flush=True)
        result = run(['g++'] + flags + options + [ROOT / 'diagnostics/check_local_page.cpp',
                                               ROOT / 'learned-index/src/unified/runner.cpp', '-o', exe])
        identity['compiler_diagnostics'][kind] = result.stderr
        identity['artifacts'][str(exe.relative_to(out))] = sha(exe)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result = run([exe], env)
        assert not result.stderr
        records[kind] = {'synthetic': [json.loads(line) for line in result.stdout.splitlines()]}
        assert records[kind]['synthetic'][-1]['status'] == 'passed'
        for cell, item in inputs.items():
            result = run([exe, WORKSPACE / item['rows_path']], env)
            assert not result.stderr
            records[kind][cell] = [json.loads(line) for line in result.stdout.splitlines()]
            assert records[kind][cell][-1]['status'] == 'passed'
            print(kind, cell, records[kind][cell][-1], flush=True)
        save('gates.json', records)
        print(kind, records[kind]['synthetic'][-1], flush=True)
    assert records['normal'] == records['sanitized'], 'cross-build semantic/counter mismatch'
    for path, digest in identity['sources'].items():
        assert sha(WORKSPACE / path) == digest, ('source changed during gate', path)
    save('identity.json', identity)
    log.close()
    for item in inputs.values():
        assert sha(WORKSPACE / item['rows_path']) == item['rows_sha256']
    save('verification.json', {'status': 'passed', 'builds': list(records),
                              'checks_per_build': {cell: result[-1]['checks'] for cell, result in records['normal'].items()},
                              'operations_per_build': {cell: result[-1]['operations'] for cell, result in records['normal'].items()},
                              'production_adopted': False, 'next': 'F2 same-capability read/write and memory comparison'})


if __name__ == '__main__':
    main()

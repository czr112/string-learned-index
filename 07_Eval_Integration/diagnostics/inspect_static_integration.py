"""Inspect the actual frozen S3 lookup code; no source mutations or timing."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='integration_attribution_20261018')
    args = parser.parse_args()
    assert Path(args.output).name == args.output
    out = ROOT/'results'/args.output
    out.mkdir(exist_ok=False)
    previous = ROOT/'results/static_interface_20261012c'
    identity = json.loads((previous/'identity.json').read_text())
    binary = previous/'build/bench'
    assert sha(binary) == identity['binaries']['bench']
    symbols = subprocess.check_output(['nm', '-S', '-C', str(binary)], text=True)
    selected = {}
    for line in symbols.splitlines():
        m = re.match(r'([0-9a-f]+) ([0-9a-f]+) \w (.+)', line)
        if not m or '[clone .cold]' in line:
            continue
        name = m[3]
        label = None
        if name.startswith('Prototype::find('):
            label = 'prototype'
        elif 'Adapter<(static_physical::Lookup)0>::find(' in name:
            label = 'formal_learned'
        if not label:
            continue
        address, size = int(m[1], 16), int(m[2], 16)
        command = ['objdump', '-d', '-M', 'intel', '--no-show-raw-insn',
                   '--start-address='+hex(address), '--stop-address='+hex(address+size), str(binary)]
        assembly = subprocess.check_output(command, text=True)
        (out/(label+'.asm')).write_text(assembly)
        counts, calls = collections.Counter(), []
        for text in assembly.splitlines():
            insn = re.match(r'\s*[0-9a-f]+:\s+(\S+)\s*(.*)', text)
            if insn:
                counts[insn[1]] += 1
                if insn[1] == 'call':
                    calls.append(insn[2])
        selected[label] = {'address': hex(address), 'size_bytes': size, 'instructions': dict(counts),
                           'instruction_count': sum(counts.values()), 'calls': calls}
    assert set(selected) == {'prototype', 'formal_learned'}
    files = ['07_Eval_Integration/diagnostics/static_physical_blocks.hpp',
             '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp',
             '07_Eval_Integration/adapters/our_static/our_static_adapter.cpp',
             '07_Eval_Integration/adapters/our_static/our_static_adapter.h',
             '02_Code_and_Experiments/validation_e25_20260918/index.hpp']
    for path in files:
        assert sha(WORKSPACE/path) == identity['sources'][path]
    result = {'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'original_binary_sha256': sha(binary), 'inspection_source_sha256': sha(Path(__file__)),
              'frozen_sources': {p: sha(WORKSPACE/p) for p in files}, 'functions': selected,
              'scope': 'static assembly inventory; instruction counts are not cycle attribution'}
    (out/'inspection.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(selected, indent=2))


if __name__ == '__main__':
    main()

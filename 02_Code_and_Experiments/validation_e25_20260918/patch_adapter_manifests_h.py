#!/usr/bin/env python3
"""Adapter identity sync, candidate-H round (2026-10-11).

The adapters pin the shipped index.hpp/switches.hpp bytes. Both changed this round:
  index.hpp   6aa6be5c... (stage D) -> 0d256611... (repack split) -> 640660a3... (candidate H)
  switches.hpp 46a18941... -> 5486909f... (H registered in all four switch tables)
The note field and the shared adapter's version string carry the same story.
"""
import io, json, sys

BASE = '/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/adapters'
FILES = [BASE + '/our_index/benchmark-adapter.json',
         BASE + '/our_index_lits/benchmark-adapter.json',
         BASE + '/our_index_routing/benchmark-adapter.json',
         BASE + '/our_index_lits_routing/benchmark-adapter.json']

OLD_IDX = '6aa6be5c8fd5d7a4c7fe9587b81308969f1d6ac7e26fbc24d46eba65365d0a49'
NEW_IDX = '640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1'
OLD_SW = '46a18941445ab7bbba9db376ef911d911fb79eac258d85dee23ffd5abd9f44db'
NEW_SW = '5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e'

NOTE = ('"index_hpp_note": "updated 2026-10-11 (candidate-H round): 6,230 lines. Two diagnostic/'
        'candidate steps on top of the stage-D identity: the bounded repack split (rp_* work counters + '
        'w_rp_sort / w_rp_move clocks; entry holds at 40.1%/40.8% move share, RESULTS_REPACK_SPLIT_20261011.md) '
        'and candidate H, Config::primary_hole_reuse (default OFF): a new insert placement path that writes the '
        'suffix into a dead range of the same sub-block instead of repacking it, plus six branch counters. '
        'switches.hpp is 5486909f... (H registered in baseline_config, pin_frozen_switches, switches() and the '
        'differ-table). Gate check_hole_reuse + its sanitizer twin are green on mixed/tranco x ctx16/lits '
        '(RESULTS_HOLE_REUSE_20261011.md); with the switch off the shipped path is unchanged. '
        'vendor/lits_model_safe/lits_model.hpp stays 4f8674b5... (LLP64 fix, unchanged since stage D). '
        'Identity trail: 6170 (stage D) -> 6230 (repack split) -> 6329 (candidate H). '
        'Evidence: 02_Code_and_Experiments/validation_e25_20260918/RESULTS_REPACK_SPLIT_20261011.md, '
        'RESULTS_HOLE_REUSE_20261011.md",')

for f in FILES:
    s = io.open(f, encoding='utf-8').read()
    for old, new, tag in ((OLD_IDX, NEW_IDX, 'index'), (OLD_SW, NEW_SW, 'switches')):
        line_old = '"%s_hpp_sha256": "%s"' % (tag, old)
        line_new = '"%s_hpp_sha256": "%s"' % (tag, new)
        n = s.count(line_old)
        if n != 1:
            sys.stderr.write('%s hash: count=%d in %s\n' % (tag, n, f))
            sys.exit(2)
        s = s.replace(line_old, line_new)
    i = s.find('"index_hpp_note":')
    if i < 0:
        sys.stderr.write('note not found in %s\n' % f)
        sys.exit(2)
    j = s.find('\n', i)
    s = s[:i] + NOTE + s[j:]
    json.loads(s)   # validate before writing
    io.open(f, 'w', encoding='utf-8', newline='').write(s)
    print('UPDATED', f)

# The shared adapter's version string carries the same identity.
cpp = BASE + '/our_index/our_index_adapter.cpp'
s = io.open(cpp, encoding='utf-8').read()
line_old = '"index.hpp sha256 %s"' % OLD_IDX
line_new = '"index.hpp sha256 %s"' % NEW_IDX
n = s.count(line_old)
if n != 1:
    sys.stderr.write('adapter cpp version string: count=%d\n' % n)
    sys.exit(2)
s = s.replace(line_old, line_new)
marker = ('    // Moved again for the candidate-H round (2026-10-11): the same diagnostic split of\n'
          '    // repack_subblock, then Config::primary_hole_reuse -- a new placement path behind a\n'
          '    // default-OFF switch whose gate is green on all four cells; the shipped configuration\n'
          '    // is untouched (the switch is off, and the split clocks compile out of every build).\n')
anchor = '    descriptor.implementation_version ='
n = s.count(anchor)
if n != 1:
    sys.stderr.write('adapter cpp anchor: count=%d\n' % n)
    sys.exit(2)
s = s.replace(anchor, marker + anchor)
io.open(cpp, 'w', encoding='utf-8', newline='').write(s)
print('UPDATED', cpp)
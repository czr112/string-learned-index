import io, json, sys

BASE = '/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/adapters'
FILES = [BASE + '/our_index/benchmark-adapter.json',
         BASE + '/our_index_lits/benchmark-adapter.json',
         BASE + '/our_index_routing/benchmark-adapter.json',
         BASE + '/our_index_lits_routing/benchmark-adapter.json']

OLD_IDX = '2bbcc23b4e02aed96eec16a1e482d07a503a34042c86e3ce70157d0d193efcc6'
NEW_IDX = '6aa6be5c8fd5d7a4c7fe9587b81308969f1d6ac7e26fbc24d46eba65365d0a49'
OLD_SW = '1e0bccc0800dcd975e841fe6ad4f38fe2e370f1db5efeb5ddc8455a41040efe3'
NEW_SW = '46a18941445ab7bbba9db376ef911d911fb79eac258d85dee23ffd5abd9f44db'

NOTE = ('"index_hpp_note": "updated 2026-10-11 (stage D write-path entry round): index.hpp 6,170 lines. '
        'The change is DIAGNOSTIC-ONLY plus a switch-table completion: E25_DIAG counters for the placement '
        'walk re-derivation (put_primary_calls / fc_calls / fc_probes / fc_gap / hint_*), two E30_WRITE clocks '
        '(w_walk, w_fc), and the three Config bools the S0 audit found missing from the switch white list '
        '(offset_directory / write_simd / write_locate), which are now also in switches(). The shipped path is '
        'unchanged; switches.hpp was touched for the same reason. Identity trail: 5423 -> 5593 (routing) -> 5629 '
        '(low-activity switch) -> 5673 (low-activity counters) -> 5733 (C-2 trackers) -> 5832 (E3 write-path '
        'clocks) -> 6124 (stage D entry device) -> 6170 (stage D supplementary check). '
        'ALSO CHANGED, recorded here because the LITS arm depends on it: vendor/lits_model_safe/lits_model.hpp '
        'is now 4f8674b576f6d4ce2045e5b5a2106c7a61cb830c4df88febfdfbd34641ac93e3 (was '
        'b5a1c282...), a one-token LLP64 portability fix (1UL << 52 -> 1ULL << 52) that is numerically '
        'identical on LP64; patch_lits_safety.py carries the two assertions. '
        'Evidence: 02_Code_and_Experiments/validation_e25_20260918/RESULTS_WRITE_RESIDUAL_20261010.md",')

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

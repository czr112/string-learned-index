# -*- coding: utf-8 -*-
"""Re-pin the four adapter manifests for the P0 byte-order fix.

The manifests record the round's files INSIDE `source_identity.index_hpp_note` (prose), so the fix
is recorded the same way: one appended paragraph naming the changed files with their new hashes.
The paragraph also says which earlier figures the change does NOT reach, because the timing record
stays bound to the identity it was measured on.
"""
import hashlib, json, os

V = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'

def sha(rel):
    return hashlib.sha256(open(os.path.join(V, rel), 'rb').read()).hexdigest()

model = sha('model/equal_depth_dir.hpp')
gate_old = sha('check_depth_dir.cpp')
gate_trunc = sha('check_depth_dir_trunc.cpp')
gate_new = sha('check_depth_dir_order.cpp')
tools = sha('tools_build.py')

PREV_MODEL = '6755f3caef472a878805c330059cf184a007704e9bb4a165d40076b1a27cbe9b'

paragraph = (
    ' P0 byte-order fix (2026-10-11, AUDIT_DEPTH_DIR_ORDER_20261011.md): the directory search '
    'compared `sep[i] < q[i]` on plain char, which is signed on x86 and put every byte >= 0x80 '
    'below every byte < 0x80; both equal-depth directories were affected. predict_group now '
    'compares through static_cast<unsigned char> (net zero for ASCII input, which is why every '
    'corpus figure above stands unchanged and keeps its old identity), and the audit counterexample '
    'is now a formal gate: model/equal_depth_dir.hpp %s (was %s), new gate check_depth_dir_order.cpp '
    '%s (plus its ASan/UBSan twin), check_depth_dir.cpp %s and check_depth_dir_trunc.cpp %s carry '
    'the same counterexample, tools_build.py %s. Gate evidence: logs/order_fix_gate_20261011.log '
    '(three gates x three corpora plus nine sanitizer cells, all rc=0) and '
    'RESULTS_DEPTH_DIR_ORDER_FIX_20261011.md.' % (model, PREV_MODEL, gate_new, gate_old, gate_trunc,
                                                  tools))

for name in ('our_index', 'our_index_lits', 'our_index_routing', 'our_index_lits_routing'):
    p = os.path.join(E, 'adapters', name, 'benchmark-adapter.json')
    d = json.load(open(p, encoding='utf-8'))
    note = d['source_identity']['index_hpp_note']
    if 'P0 byte-order fix' in note:
        print('already pinned:', name); continue
    if PREV_MODEL not in note:
        raise SystemExit('FATAL %s: the TRUNCDIR note does not quote the pre-fix model hash' % name)
    d['source_identity']['index_hpp_note'] = note.rstrip() + paragraph
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(d, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print('re-pinned', name)

print('model/equal_depth_dir.hpp', model)
print('check_depth_dir_order.cpp ', gate_new)
print('check_depth_dir.cpp       ', gate_old)
print('check_depth_dir_trunc.cpp ', gate_trunc)

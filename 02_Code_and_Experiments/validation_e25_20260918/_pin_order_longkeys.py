import hashlib, io, json, os
V = r'02_Code_and_Experiments/validation_e25_20260918/'
E = r'07_Eval_Integration/adapters'
OLD = 'bfb6214f7d7e8e6710697f2467ad627ccfd1eca53df403937865ddfa0a63bd8c'
NEW = hashlib.sha256(open(V + 'check_depth_dir_order.cpp', 'rb').read()).hexdigest()
for name in ('our_index', 'our_index_lits', 'our_index_routing', 'our_index_lits_routing'):
    p = os.path.join(E, name, 'benchmark-adapter.json')
    d = json.load(open(p, encoding='utf-8'))
    note = d['source_identity']['index_hpp_note']
    if OLD not in note:
        raise SystemExit('FATAL %s: order-gate hash not in the note' % name)
    d['source_identity']['index_hpp_note'] = note.replace(OLD, NEW)
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(d, f, ensure_ascii=False, indent=2); f.write('\n')
    print('updated', name)
print('order gate hash now', NEW)

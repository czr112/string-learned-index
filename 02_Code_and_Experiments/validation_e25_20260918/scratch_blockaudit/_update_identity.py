import hashlib, json, os
V = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'

def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()

idx = sha(os.path.join(V, 'index.hpp'))
sw = sha(os.path.join(V, 'switches.hpp'))
br = sha(os.path.join(V, 'model', 'block_linear_rank.hpp'))
print('index.hpp  ', idx)
print('switches.hpp', sw)
print('block_linear_rank.hpp', br)

OLD_IDX = 'b20c83cab42787078508d5af6dd8a9e3c512fb0731618b9b0b81dc8fc64056d6'
OLD_SW = '72856bdc89f994e489c5b41c5e87a21dbafb51873c41f08e2550b71efc3edba7'

note_add = (' BlockLinearRank round (2026-10-11): the third ModelBackend, Config::model_backend = '
            'BlockLinearRank (default stays Context16), backed by model/block_linear_rank.hpp %s -- a '
            'region-scoped rank model ported from the teammate repo Zephyr761/Learned-Index-Model @ '
            '60837a88 (9-bit order-preserving encode + segmented linear fit, block_size=1, 7 encoded '
            'bytes, 8 segments, no parameter search). Region-side: calibrate_block_rank, the three-way '
            'dispatch in canonical_group/predict_for/demand_profile, mapping_identity() over the fitted '
            'model for the R1/R2 preset guard, and the segment bytes counted in memory_breakdown(). '
            'switches.hpp also carries the three-valued backend_name() and a differ-table that now '
            'compares the enum itself (the old "== LitsHpt" encoding made Context16 vs BlockLinearRank '
            'look like no difference). Gate check_block_rank (+ san twin) green on mixed/skew/tranco and '
            'under ASan/UBSan; the first-version mapping is CORRECT but a measured entry regression '
            '(2.0-3.8x median per-region primary probes), so it stays a default-OFF candidate: '
            'RESULTS_BLOCK_RANK_ENTRY_20261011.md. With the backend left at Context16 the shipped path '
            'is unchanged except that memory_breakdown() now counts the (empty) rank model object.')

for name in ('our_index', 'our_index_lits', 'our_index_routing', 'our_index_lits_routing'):
    p = os.path.join(E, 'adapters', name, 'benchmark-adapter.json')
    d = json.load(open(p, encoding='utf-8'))
    si = d['source_identity']
    assert si['index_hpp_sha256'] == OLD_IDX, name
    assert si['switches_hpp_sha256'] == OLD_SW, name
    si['index_hpp_sha256'] = idx
    si['switches_hpp_sha256'] = sw
    si['index_hpp_note'] = si['index_hpp_note'].rstrip() + note_add
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(d, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print('updated', name)

lp = os.path.join(E, 'diagnostics', 'run_owner_lifecycle.py')
s = open(lp, encoding='utf-8', newline='').read()
assert s.count(OLD_IDX) == 1 and s.count(OLD_SW) == 1
s = s.replace(OLD_IDX, idx).replace(OLD_SW, sw)
open(lp, 'w', encoding='utf-8', newline='').write(s)
print('updated run_owner_lifecycle.py pins')

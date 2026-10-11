import hashlib, json, os
V = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'
def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()

idx = sha(os.path.join(V,'index.hpp')); sw = sha(os.path.join(V,'switches.hpp'))
hdr = sha(os.path.join(V,'model','equal_depth_dir.hpp'))
gate = sha(os.path.join(V,'check_depth_dir_trunc.cpp'))
bench = sha(os.path.join(V,'bench_depth_dir.cpp'))
runner = sha(os.path.join(V,'run_depth_dir_ab.py'))
tools = sha(os.path.join(V,'tools_build.py'))
for n,v in (('index.hpp',idx),('switches.hpp',sw),('equal_depth_dir.hpp',hdr),
            ('check_depth_dir_trunc.cpp',gate),('bench_depth_dir.cpp',bench),
            ('run_depth_dir_ab.py',runner),('tools_build.py',tools)):
    print('%-26s %s' % (n, v))

OLD_IDX = '8688225e23bae7aa0d54daafc4c19ceab61f7d250fb66431c2be832b2a983f82'
OLD_SW  = '12a497a283cd25b0e6d67d74bfdfbc9815037cbaf192fa6ef244b00ca54278ee'
note = (' TRUNCDIR round (2026-10-11): the fifth ModelBackend, Config::model_backend = '
        'EqualDepthDirTrunc (default stays Context16) -- the same equal-rank separator directory as '
        'EqualDepthDir, with the truncated separators of TRUNCDIR_ENTRY_DESIGN.md section 2: each '
        'threshold stores the shortest prefix of its suffix that still compares greater than the key '
        'immediately to its left (duplicate-run boundaries keep the full suffix). Occurrence: '
        'model/equal_depth_dir.hpp %s, gate check_depth_dir_trunc.cpp %s, A/B bench_depth_dir.cpp %s '
        '+ run_depth_dir_ab.py %s. The default path is unchanged: with the backend left at Context16 '
        'nothing is fitted and switches.hpp gains only the backend_name case. switches.hpp is %s.')
for name in ('our_index', 'our_index_lits', 'our_index_routing', 'our_index_lits_routing'):
    p = os.path.join(E, 'adapters', name, 'benchmark-adapter.json')
    d = json.load(open(p, encoding='utf-8'))
    si = d['source_identity']
    assert si['index_hpp_sha256'] == OLD_IDX, (name, si['index_hpp_sha256'])
    assert si['switches_hpp_sha256'] == OLD_SW, (name, si['switches_hpp_sha256'])
    si['index_hpp_sha256'] = idx
    si['switches_hpp_sha256'] = sw
    si['index_hpp_note'] = si['index_hpp_note'].rstrip() + (note % (hdr, gate, bench, runner, sw))
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(d, f, ensure_ascii=False, indent=2); f.write('\n')
lp = os.path.join(E, 'diagnostics', 'run_owner_lifecycle.py')
t = open(lp, encoding='utf-8', newline='').read()
assert t.count(OLD_IDX) == 1 and t.count(OLD_SW) == 1
open(lp, 'w', encoding='utf-8', newline='').write(t.replace(OLD_IDX, idx).replace(OLD_SW, sw))
print('re-pinned 4 manifests + run_owner_lifecycle.py')

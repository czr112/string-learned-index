import hashlib, json, os
V = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'
def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()

idx = sha(os.path.join(V,'index.hpp')); sw = sha(os.path.join(V,'switches.hpp'))
hdr = sha(os.path.join(V,'model','equal_depth_dir.hpp'))
gate = sha(os.path.join(V,'check_depth_dir.cpp')); bench = sha(os.path.join(V,'bench_depth_dir.cpp'))
tools = sha(os.path.join(V,'tools_build.py'))
for n,v in (('index.hpp',idx),('switches.hpp',sw),('equal_depth_dir.hpp',hdr),
            ('check_depth_dir.cpp',gate),('bench_depth_dir.cpp',bench),('tools_build.py',tools)):
    print('%-24s %s' % (n, v))

OLD_IDX = 'f553630d5cc31296a5c654c67127e7123c04056a3c214b2e91200f5ae05fa78e'
OLD_SW = 'f09dc3f6dbff1031d8aa8667ceb45822b784f419db7de9dbc026a4bdda62ecf1'
note = (' EqualDepthDir round (2026-10-11): the fourth ModelBackend, Config::model_backend = '
        'EqualDepthDir (default stays Context16), backed by model/equal_depth_dir.hpp %s -- a '
        'region-scoped separator directory at equal key RANKS (threshold j at rank floor(j*n/G), '
        'group = number of thresholds the suffix compares >= against). Region-side: '
        'calibrate_depth_dir, the four-way dispatch in canonical_group/predict_for/demand_profile, '
        'mapping_identity() over the directory, the separator arena counted in memory_breakdown(), '
        'and DepthDirStats. Correctness: check_depth_dir + sanitizer twin green on mixed/skew/tranco '
        '(131,626 checks per corpus, 0 failures) including an independent recount of the fit on '
        'corpus slices, the exact depth property (12/13 pattern on n=100/G=8), duplicate-key and '
        'domain suites, and a rotation negative control. Entry: primary probes collapse to 1.00 per '
        'query and overflow to 0. Paired A/B (5x4, paired within repetition): skew faster '
        '(0.903 static / 0.779 post, 5/5), mixed slower (1.142 static, 0/5), tranco unresolved '
        '(1.257 against a 1.321 noise floor); build and maintenance faster on all three (0.87-0.95 '
        'and 0.43-0.71, 5/5). Background is NOT on, on the plan\'s rule (mixed is the main target): '
        'RESULTS_DEPTH_DIR_20261011.md. switches.hpp is %s.')
for name in ('our_index', 'our_index_lits', 'our_index_routing', 'our_index_lits_routing'):
    p = os.path.join(E, 'adapters', name, 'benchmark-adapter.json')
    d = json.load(open(p, encoding='utf-8'))
    si = d['source_identity']
    assert si['index_hpp_sha256'] == OLD_IDX and si['switches_hpp_sha256'] == OLD_SW, name
    si['index_hpp_sha256'] = idx
    si['switches_hpp_sha256'] = sw
    si['index_hpp_note'] = si['index_hpp_note'].rstrip() + (note % (hdr, sw))
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(d, f, ensure_ascii=False, indent=2); f.write('\n')
lp = os.path.join(E, 'diagnostics', 'run_owner_lifecycle.py')
t = open(lp, encoding='utf-8', newline='').read()
assert t.count(OLD_IDX) == 1 and t.count(OLD_SW) == 1
open(lp, 'w', encoding='utf-8', newline='').write(t.replace(OLD_IDX, idx).replace(OLD_SW, sw))
print('re-pinned 4 manifests + run_owner_lifecycle.py')

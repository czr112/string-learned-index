import hashlib, os
IDX = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp'
s = open(IDX, 'r', encoding='utf-8', newline='').read()

def sub(t, a, r, label):
    if t.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, t.count(a)))
    return t.replace(a, r)

# 1) the never-executed fit-time counter
a = '''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
    // E25_DIAG only, and zero in every other build: the time spent INSIDE fit(), summed over
    // the region fits that happened since the last reset. The round has to be able to say
    // whether a build-cost difference is the rank model or the layout around it, and only the
    // fit itself answers that question. Compiled out of timing builds like every counter here.
    std::uint64_t fit_ns = 0;
};'''
r = '''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
};'''
s = sub(s, a, r, 'fit_ns field')

# 2) the diag clock around the fit
a = '''#ifdef E25_DIAG
        const auto block_rank_fit_t0 = std::chrono::steady_clock::now();
#endif
        block_rank_ = blockrank::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
#ifdef E25_DIAG
        block_rank_stats_.fit_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - block_rank_fit_t0).count());
#endif
'''
r = '''        block_rank_ = blockrank::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
'''
s = sub(s, a, r, 'fit clock')

# 3) its include and comment block
a = '''#ifdef E25_DIAG
// The rank model's fit timing needs a clock. E14 and E30 carry their own includes, but both of
// those blocks are compiled out unless their own switch is on, so this one does not borrow
// theirs -- and does not make <chrono> part of a timing build either.
#include <chrono>
namespace validation {'''
r = '''#ifdef E25_DIAG
namespace validation {'''
s = sub(s, a, r, 'chrono include')

open(IDX, 'w', encoding='utf-8', newline='').write(s)
h = hashlib.sha256(open(IDX,'rb').read()).hexdigest()
print('index.hpp reverted, sha256', h)

# re-pin everywhere the old hash of THIS revision was declared
import json
OLD = '46fdf520d82804e595727c267c428c5fbbb9d8c0da02e09ffe4ffc9f413a6957'
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'
for name in ('our_index', 'our_index_lits', 'our_index_routing', 'our_index_lits_routing'):
    p = os.path.join(E, 'adapters', name, 'benchmark-adapter.json')
    d = json.load(open(p, encoding='utf-8'))
    assert d['source_identity']['index_hpp_sha256'] == OLD, name
    d['source_identity']['index_hpp_sha256'] = h
    with open(p, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(d, f, ensure_ascii=False, indent=2); f.write('\n')
lp = os.path.join(E, 'diagnostics', 'run_owner_lifecycle.py')
t = open(lp, encoding='utf-8', newline='').read()
assert t.count(OLD) == 1
open(lp, 'w', encoding='utf-8', newline='').write(t.replace(OLD, h))
print('re-pinned 4 manifests + run_owner_lifecycle.py to', h[:16])

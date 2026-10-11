import hashlib
IDX = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp"
src = open(IDX, 'r', encoding='utf-8', newline='').read()
assert '\r\n' not in src

def sub(t, a, r, label):
    n = t.count(a)
    if n != 1:
        raise SystemExit('ANCHOR FAIL %s: %d\n%s' % (label, n, a[:200]))
    return t.replace(a, r)

# 1) the counter field
a = '''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
};'''
r = '''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
    // E25_DIAG only, and zero in every other build: the time spent INSIDE fit(), summed over
    // the region fits that happened since the last reset. The round has to be able to say
    // whether a build-cost difference is the rank model or the layout around it, and only the
    // fit itself answers that question. Compiled out of timing builds like every counter here.
    std::uint64_t fit_ns = 0;
};'''
src = sub(src, a, r, 'fit_ns field')

# 2) time the fit call
a = '''        block_rank_ = blockrank::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
        use_block_rank_ = true;
        ++block_rank_stats_.fits;'''
r = '''#ifdef E25_DIAG
        const auto block_rank_fit_t0 = std::chrono::steady_clock::now();
#endif
        block_rank_ = blockrank::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
#ifdef E25_DIAG
        block_rank_stats_.fit_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - block_rank_fit_t0).count());
#endif
        use_block_rank_ = true;
        ++block_rank_stats_.fits;'''
src = sub(src, a, r, 'fit timing')

open(IDX, 'w', encoding='utf-8', newline='').write(src)
print('index.hpp timing patch applied, sha256', hashlib.sha256(open(IDX,'rb').read()).hexdigest())

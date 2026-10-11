import hashlib
IDX = r"D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp"
s = open(IDX, 'r', encoding='utf-8', newline='').read()

inc = '''#ifdef E25_DIAG
// The rank model's fit timing needs a clock. E14 and E30 carry their own includes, but both of
// those blocks are compiled out unless their own switch is on, so this one does not borrow
// theirs -- and does not make <chrono> part of a timing build either.
#include <chrono>
namespace validation {
namespace e25 {
struct Counts {'''
inc_old = '''#ifdef E25_DIAG
namespace validation {
namespace e25 {
struct Counts {'''
assert s.count(inc) == 1
x2 = s.replace(inc, inc_old)

tim = '''#ifdef E25_DIAG
        const auto block_rank_fit_t0 = std::chrono::steady_clock::now();
#endif
        block_rank_ = blockrank::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
#ifdef E25_DIAG
        block_rank_stats_.fit_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - block_rank_fit_t0).count());
#endif
        use_block_rank_ = true;'''
tim_old = '''        block_rank_ = blockrank::RegionModel::fit(keys.data(), keys.size(), prefix_.size(), groups_);
        use_block_rank_ = true;'''
assert x2.count(tim) == 1
x1 = x2.replace(tim, tim_old)

fitn = '''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
    // E25_DIAG only, and zero in every other build: the time spent INSIDE fit(), summed over
    // the region fits that happened since the last reset. The round has to be able to say
    // whether a build-cost difference is the rank model or the layout around it, and only the
    // fit itself answers that question. Compiled out of timing builds like every counter here.
    std::uint64_t fit_ns = 0;
};'''
fitn_old = '''    std::uint64_t segments_total = 0;   // sum of segment counts over the fitted regions
    std::uint64_t fits = 0;             // fit() calls, boundary early-outs included
};'''
assert x1.count(fitn) == 1
x0 = x1.replace(fitn, fitn_old)

def h(t): return hashlib.sha256(t.encode('utf-8')).hexdigest()
print('current      ', h(s))
print('after patch1 ', h(x2), '(expected 8a05acfb...)')
print('pristine     ', h(x1))
print('pristine no-fitns', h(x0))
print('expect pristine b20c83cab42787078508d5af6dd8a9e3c512fb0731618b9b0b81dc8fc64056d6')
open(IDX + '.reverse_check', 'w', encoding='utf-8', newline='').write(x0)

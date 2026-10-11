import hashlib
IDX = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp'
GATE = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\check_block_rank.cpp'

def sub(path, a, r, label):
    s = open(path, 'r', encoding='utf-8', newline='').read()
    if s.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, s.count(a)))
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(a, r))

sub(IDX,
'''    // True when this region was mapped by the rank backend.
    bool uses_block_rank() const { return use_block_rank_; }''',
'''    // True when this region was mapped by the rank backend.
    bool uses_block_rank() const { return use_block_rank_; }
    // The group the PLACEMENT path would use for this full key, exposed for gates and
    // diagnostics that must check order-preservation directly instead of inferring it from
    // code. It is deliberately the same call the demand pass and the placement loop make.
    std::uint32_t placement_group(std::string_view full_key) const {
        return canonical_group(full_key);
    }''',
 'index placement_group')

sub(GATE,
'''        const auto g = idx.region(r).canonical_group(kv.first);''',
'''        const auto g = idx.region(r).placement_group(kv.first);''',
 'gate placement_group')

print('sha256 index.hpp', hashlib.sha256(open(IDX,'rb').read()).hexdigest())
print('sha256 gate     ', hashlib.sha256(open(GATE,'rb').read()).hexdigest())

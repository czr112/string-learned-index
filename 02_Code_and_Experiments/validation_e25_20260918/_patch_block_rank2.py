import hashlib

IDX = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\index.hpp'

# Second, additive step of the BlockLinearRank integration (2026-10-11). The first step
# (_patch_block_rank.py) added the backend. This step adds NOTHING to any code path: it adds two
# READ-ONLY accessors to Region so the P2 entry can check the mapping the placement pass and the
# query path actually use. `group_of()` cannot serve that purpose -- it is deliberately the
# REFERENCE model's answer and is not backend-dispatched -- so without these the order property
# the layout rests on would only be checkable by reading the code.

src = open(IDX, 'r', encoding='utf-8', newline='').read()
assert '\r\n' not in src, 'index.hpp unexpectedly has CRLF'
orig_len = len(src)

a = '''    // True when this region was mapped by the rank backend.
    bool uses_block_rank() const { return use_block_rank_; }
    blockrank::Status block_rank_status() const { return block_rank_.status(); }
'''
r = '''    // True when this region was mapped by the rank backend.
    bool uses_block_rank() const { return use_block_rank_; }
    blockrank::Status block_rank_status() const { return block_rank_.status(); }

    // The group THIS REGION'S RANK MODEL maps a full key to. Not the same quantity as group_of()
    // below: that one reports the reference model's answer on a suffix and is intentionally not
    // backend-dispatched, while this is the mapping the placement pass and the query path read.
    // The P2 entry uses it to check the structural precondition the layout depends on -- that the
    // map is non-decreasing in key order and stays inside [0, groups_) -- and a gate cannot check
    // what it cannot read.
    std::uint32_t block_rank_group(std::string_view full_key) const {
        if (!use_block_rank_)
            throw std::logic_error("block_rank_group: this region is not on the rank backend");
        return block_rank_.predict_group(full_key);
    }

    // The fitted model itself, read-only, for the entry's recomputation check: the checker fits a
    // RegionModel of its own from the region's key range and requires the two maps to agree for
    // every key. Exposed as const& so nothing can train or mutate this region's model through it.
    const blockrank::RegionModel& block_rank_model() const {
        if (!use_block_rank_)
            throw std::logic_error("block_rank_model: this region is not on the rank backend");
        return block_rank_;
    }
'''
n = src.count(a)
assert n == 1, 'anchor found %d times' % n
src = src.replace(a, r)

open(IDX, 'w', encoding='utf-8', newline='').write(src)
print('index.hpp accessors added:', orig_len, '->', len(src), 'chars')
print('sha256', hashlib.sha256(open(IDX, 'rb').read()).hexdigest())

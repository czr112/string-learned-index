import hashlib
GATE = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\check_block_rank.cpp'
TOOLS = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\tools_build.py'

def sub(path, a, r, label):
    s = open(path, 'r', encoding='utf-8', newline='').read()
    if s.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, s.count(a)))
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(a, r))

# 1) collision family excludes the first key ("a" is a genuine non-colliding neighbour)
sub(GATE,
'''        // Every colliding key must land in the same group.
        const auto g0 = m.predict_group(keys[1]);
        for (const auto& k : keys) require(m.predict_group(k) == g0, "collision keys share a group");''',
'''        // Every colliding key must land in the same group. keys[0] == "a" is a deliberate
        // NON-colliding neighbour (its coordinate is 0), so the family starts at index 1 --
        // asserting over the whole vector would demand that a different coordinate share the
        // answer, which is not what "equal coordinates must not be split" says.
        const auto g0 = m.predict_group(keys[1]);
        for (std::size_t i = 1; i < keys.size(); ++i)
            require(m.predict_group(keys[i]) == g0, "collision keys share a group");''',
 'collision family')

# 2) erased keys: the FAILURE is "still found", not "gone". The first version counted the
#    correct outcome and asserted it never happened.
sub(GATE,
'''        for (std::size_t i = 0; i < 1000; ++i)
            if (!blk.find(added[i], qmode).found) ++dyn_absent_gone;''',
'''        for (std::size_t i = 0; i < 1000; ++i)
            if (blk.find(added[i], qmode).found) ++dyn_absent_gone;   // a survivor is the failure''',
 'erase direction')

# 3) the entry counters live behind E12_TICK, so both targets are built with -DE12_COUNT
sub(TOOLS,
'''    "check_scan_contract": ["-DE25_DIAG"],''',
'''    # BlockLinearRank round: the gate reports the entry numbers (primary probes, full
    # comparisons, overflow entries). Those fields are written through E12_TICK, which is a
    # no-op in a shipped build, so the gate is built WITH the counters. A gate is not a
    # timing build, so the counters cost nothing that is measured.
    "check_block_rank": ["-DE12_COUNT"],
    "san_check_block_rank": ["-DE12_COUNT"],
    "check_scan_contract": ["-DE25_DIAG"],''',
 'EXTRA')

print('gate sha256    ', hashlib.sha256(open(GATE,'rb').read()).hexdigest())
print('tools sha256   ', hashlib.sha256(open(TOOLS,'rb').read()).hexdigest())

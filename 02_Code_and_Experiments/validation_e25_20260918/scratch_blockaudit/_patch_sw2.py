import hashlib, os
SW = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\switches.hpp'
s = open(SW, 'r', encoding='utf-8', newline='').read()
a = '''        case validation::ModelBackend::BlockLinearRank: return "BlockLinearRank";'''
r = '''        case validation::ModelBackend::BlockLinearRank: return "BlockLinearRank";
        case validation::ModelBackend::EqualDepthDir: return "EqualDepthDir";'''
if s.count(a) != 1:
    raise SystemExit('anchor %d' % s.count(a))
s = s.replace(a, r)
# the differ-table row is already enum-vs-enum, so a fourth value needs no table change -- say so.
a2 = '''        // Three-valued: the row now tests the enum itself, so Context16 vs BlockLinearRank IS
        // a difference and an arm pair differing only in the backend is checked rather than
        // waved through.
        {"model_backend", a.model_backend != b.model_backend},'''
r2 = '''        // Valued by the ENUM, not by a two-way test: this row was "== LitsHpt" until the
        // BlockLinearRank round, which made Context16 vs BlockLinearRank look like NO
        // difference. Comparing the enum keeps that fixed for every value the enum gains --
        // EqualDepthDir needed no change here for exactly that reason.
        {"model_backend", a.model_backend != b.model_backend},'''
if s.count(a2) != 1:
    raise SystemExit('anchor2 %d' % s.count(a2))
s = s.replace(a2, r2)
open(SW, 'w', encoding='utf-8', newline='').write(s)
print('switches.hpp', hashlib.sha256(open(SW,'rb').read()).hexdigest())

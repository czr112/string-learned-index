import hashlib
P = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918\tools_build.py'
s = open(P, 'r', encoding='utf-8', newline='').read()

def sub(t, a, r, label):
    if t.count(a) != 1:
        raise SystemExit('ANCHOR FAIL %s (%d)' % (label, t.count(a)))
    return t.replace(a, r)

a = '''       "check_sround": "check_sround.cpp",
       "san_check_sround": "check_sround.cpp",'''
r = a + '''
       # BlockLinearRank round (2026-10-11): one source, two targets -- the gate and its
       # sanitizer twin, so the two builds of the same gate cannot drift apart.
       "check_block_rank": "check_block_rank.cpp",
       "san_check_block_rank": "check_block_rank.cpp",'''
s = sub(s, a, r, 'SRC')

a = '''    "san_check_sround",
}'''
r = '''    "san_check_sround",
    # BlockLinearRank round: the gate builds small indexes from binary keys (NUL, high
    # bytes, 4096-byte keys), mutates them, and walks per-region group vectors -- new code
    # over the encode/fit path, so it runs under the sanitizer like its siblings.
    "san_check_block_rank",
}'''
s = sub(s, a, r, 'SAN_TARGETS')

open(P, 'w', encoding='utf-8', newline='').write(s)
print('tools_build.py patched', hashlib.sha256(open(P,'rb').read()).hexdigest())

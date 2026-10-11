P = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md'
s = open(P, encoding='utf-8', newline='').read()
bad = 'run_owner_lifecycle.py`。\\n  并发写入事故：'
if s.count(bad) != 1:
    raise SystemExit('anchor %d' % s.count(bad))
s = s.replace(bad, 'run_owner_lifecycle.py`。\n  并发写入事故：')
open(P, 'w', encoding='utf-8', newline='').write(s)
print('fixed literal \\n')

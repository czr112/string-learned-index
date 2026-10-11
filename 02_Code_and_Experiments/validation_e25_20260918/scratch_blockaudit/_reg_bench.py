import hashlib, os
D = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
T = os.path.join(D, 'tools_build.py')
t = open(T, 'r', encoding='utf-8', newline='').read()
a = '''       "check_depth_dir": "check_depth_dir.cpp",
       "san_check_depth_dir": "check_depth_dir.cpp",'''
r = a + '''
       # EqualDepthDir A/B: two arms, position rotated, paired within a repetition.
       "bench_depth_dir": "bench_depth_dir.cpp",'''
if t.count(a) != 1:
    raise SystemExit('anchor %d' % t.count(a))
open(T, 'w', encoding='utf-8', newline='').write(t.replace(a, r))
print('tools_build.py', hashlib.sha256(open(T,'rb').read()).hexdigest())

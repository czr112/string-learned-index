import hashlib, os, json
V = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'
def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
diag = sha(os.path.join(V,'diag_threshold_entry.cpp'))
tools = sha(os.path.join(V,'tools_build.py'))
idx = sha(os.path.join(V,'index.hpp'))
rec = json.load(open(os.path.join(V,'build','diag_threshold_entry.built.json'), encoding='utf-8'))
print('diag', diag[:16], 'tools', tools[:16], 'index', idx[:16])
print('build_id', rec['build_id'], 'exe_matches', sha(os.path.join(V,'build','diag_threshold_entry.exe')) == rec['exe_sha256'])
print('record index.hpp', rec['sources'].get('index.hpp','?')[:16], 'record diag', rec['sources'].get('diag_threshold_entry.cpp','?')[:16])

import hashlib, json, os, time
V = r'D:\StringLearnedIndex_Workspace\02_Code_and_Experiments\validation_e25_20260918'
E = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration'
def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()

now = {}
for n in ('index.hpp','switches.hpp','check_block_rank.cpp','tools_build.py', r'model\block_linear_rank.hpp'):
    p = os.path.join(V, n)
    now[n] = sha(p)
    print('%-26s %s  %s' % (n, now[n][:16], time.strftime('%H:%M:%S', time.localtime(os.stat(p).st_mtime))))

print('--- adapter manifests vs disk ---')
ok = True
for name in ('our_index','our_index_lits','our_index_routing','our_index_lits_routing'):
    d = json.load(open(os.path.join(E,'adapters',name,'benchmark-adapter.json'), encoding='utf-8'))
    si = d['source_identity']
    a = si['index_hpp_sha256'] == now['index.hpp']
    b = si['switches_hpp_sha256'] == now['switches.hpp']
    ok &= a and b
    print('%-22s index=%s switches=%s' % (name, 'OK' if a else 'STALE', 'OK' if b else 'STALE'))
lp = os.path.join(E,'diagnostics','run_owner_lifecycle.py')
t = open(lp, encoding='utf-8').read()
print('run_owner_lifecycle pins index:', now['index.hpp'] in t, ' switches:', now['switches.hpp'] in t)

print('--- build record binds to the tree ---')
rec = json.load(open(os.path.join(V,'build','check_block_rank.built.json'), encoding='utf-8'))
print('exe hash matches:', sha(os.path.join(V,'build','check_block_rank.exe')) == rec['exe_sha256'])
print('record has index.hpp:', rec['sources'].get('index.hpp','MISSING')[:16])
print('record has model/block_linear_rank.hpp:', rec['sources'].get('model/block_linear_rank.hpp','MISSING')[:16])
print('all pins consistent:', ok)

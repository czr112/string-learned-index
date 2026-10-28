"""Dev-only: show the recorded artifact keys and the rebuilt hash."""
import hashlib
import json
import pathlib

ws = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace')
fresh = ws / 'builds/rebuild_20261019/clean'
identity = json.loads((ws / '07_Eval_Integration/results/bm_space_20261019b/identity.json').read_text(encoding='utf-8'))
print('fresh', hashlib.sha256(fresh.read_bytes()).hexdigest())
artifacts = identity['artifacts']
print('keys', list(artifacts)[:8])
for key, value in artifacts.items():
    if 'clean' in key or 'trace' in key:
        print(key, value)

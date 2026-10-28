"""Retain raw bytes after the first runner failed to decode the shared suite's byte-key output."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

root=Path(__file__).resolve().parents[1]
ws=root.parent
out=root/'results/bm_cost_native_20261019'
meta=json.loads((out/'identity.json').read_text())
item=meta['inputs']['english_words']
command=['taskset','-c',str(meta['cpu']),str(out/'build/gate'),str(ws/item['rows_path']),str(ws/item['queries_path'])]
p=subprocess.run(command,capture_output=True,env=dict(os.environ,BLOCKLEARN_COST_PROFILE=str(out/'planner-costs.txt')),timeout=7200)
for suffix,data in [('stdout',p.stdout),('stderr',p.stderr)]:
    (out/('transport-recovery.'+suffix+'.bin')).write_bytes(data)
record={'command':command,'rc':p.returncode,'stdout_sha256':hashlib.sha256(p.stdout).hexdigest(),
        'stderr_sha256':hashlib.sha256(p.stderr).hexdigest(),
        'original_failure':'UnicodeDecodeError while decoding diagnostic stdout containing arbitrary byte keys; no valid gate record or timing produced'}
(out/'transport_failure.json').write_text(json.dumps(record,indent=2)+'\n')
print('recovery return code',p.returncode)
print(p.stdout.decode('utf-8',errors='backslashreplace')[-14000:])
print(p.stderr.decode('utf-8',errors='backslashreplace')[-4000:])

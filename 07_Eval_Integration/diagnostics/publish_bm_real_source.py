"""Publish an already downloaded and locked source using a same-directory rename."""
import hashlib
import json
from pathlib import Path

workspace=Path(__file__).resolve().parents[2]
benchmark=workspace/'07_Eval_Integration/upstream_snapshots/benchmark_003a55bf'
source=json.loads((benchmark/'datasets.lock.json').read_text())['sources']['memetracker-clusters']
target=(benchmark/'data/raw'/source['filename']).resolve()
partial=target.with_name(target.name+'.native.part')
assert target.is_relative_to(workspace) and partial.is_relative_to(workspace)
assert partial.stat().st_size==source['bytes'] and not target.exists()
h=hashlib.sha256()
with partial.open('rb') as f:
    for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
assert h.hexdigest()==source['sha256']
partial.rename(target)
info={'status':'verified','source':source,'bytes':target.stat().st_size,'sha256':h.hexdigest(),
      'publication':'same-directory rename after native hardlink publication was denied',
      'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
(target.parent/'memetracker-native-publication.json').write_text(json.dumps(info,indent=2)+'\n')
print('locked MemeTracker source published and verified')

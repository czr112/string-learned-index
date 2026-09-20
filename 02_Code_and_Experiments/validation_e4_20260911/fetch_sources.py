"""Fetch pinned public dependencies, checking every file before accepting it."""
from pathlib import Path
import concurrent.futures,hashlib,json,urllib.request
root=Path(__file__).resolve().parent
manifest=json.loads((root/'sources.json').read_text())
def fetch(item):
    p=root/item['path'];want=item['sha256']
    if p.exists() and hashlib.sha256(p.read_bytes()).hexdigest()==want:return
    request=urllib.request.Request(item['source'],headers={'User-Agent':'learned-index-reproduction'})
    with urllib.request.urlopen(request,timeout=60) as r:payload=r.read()
    if len(payload)!=item['bytes'] or hashlib.sha256(payload).hexdigest()!=want:raise RuntimeError('source hash mismatch '+item['path'])
    p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(payload)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(fetch,manifest['files']))
print('Verified',len(manifest['files']),'pinned source files')

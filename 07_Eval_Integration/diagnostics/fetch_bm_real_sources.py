"""Fetch exactly locked bytes with a slower-network budget; preprocess upstream offline."""
import hashlib
import json
from pathlib import Path
import time
import urllib.request

root=Path(__file__).resolve().parents[1]
benchmark=root/'upstream_snapshots/benchmark_003a55bf'
source=json.loads((benchmark/'datasets.lock.json').read_text())['sources']['memetracker-clusters']
directory=benchmark/'data/raw'
target=directory/source['filename']
partial=target.with_name(target.name+'.native.part')
log_path=directory/'memetracker-native-fetch.json'
assert not target.exists() and not log_path.exists()
started=time.monotonic();digest=hashlib.sha256();size=0;next_progress=16*1024*1024
info={'source':source,'download_budget_seconds':900,'reason':'upstream initial download exceeded its 300 second budget',
      'fetch_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
try:
    request=urllib.request.Request(source['url'],headers={'User-Agent':'string-benchmark-datasets/1'})
    with urllib.request.urlopen(request,timeout=30) as response, partial.open('xb') as out:
        assert response.geturl().startswith('https://')
        while True:
            block=response.read(64*1024)
            if not block:break
            size+=len(block)
            assert size<=source['bytes'] and time.monotonic()-started<=900
            digest.update(block);out.write(block)
            if size>=next_progress:
                print('downloaded',size,'of',source['bytes'],'elapsed',round(time.monotonic()-started,1),flush=True)
                next_progress+=16*1024*1024
    assert size==source['bytes'] and digest.hexdigest()==source['sha256']
    # Path.link_to is intentionally avoided: hardlink publication never overwrites.
    target.hardlink_to(partial)
    partial.unlink()
    info.update(status='verified',bytes=size,sha256=digest.hexdigest(),elapsed_seconds=time.monotonic()-started)
except BaseException as error:
    info.update(status='failed',bytes=size,elapsed_seconds=time.monotonic()-started,error=type(error).__name__)
    raise
finally:
    log_path.write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
print('locked MemeTracker source verified',flush=True)

"""Serial, non-performance acceptance for the pinned benchmark publication."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

root = Path('/mnt/d/StringLearnedIndex_Workspace/builds/project_recovery_20261031')
source = root/'publication-main/07_Eval_Integration/learned-index'
out = root/'linux-benchmark-regression'
out.mkdir(exist_ok=False)
os.environ['PATH'] = '/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin'
os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
os.environ['TMPDIR'] = str(out/'tmp')
(out/'tmp').mkdir()
cmake = shutil.which('cmake')
ctest = shutil.which('ctest')
if not cmake or not ctest:
    raise RuntimeError('Linux cmake/ctest unavailable; no installation attempted')
build = out/'build'
steps = [
    ('configure', [cmake,'-S',str(source),'-B',str(build),'-DBUILD_LEGACY_BENCHMARK=OFF','-DCMAKE_BUILD_TYPE=Debug']),
    ('build', [cmake,'--build',str(build),'--parallel','1']),
    ('ctest', [ctest,'--test-dir',str(build),'--output-on-failure','--parallel','1']),
    ('lookup_workload', [sys.executable,'-B',str(source/'tests/lookup_workload_test.py')]),
    ('real_dataset', [sys.executable,'-B',str(source/'tests/real_dataset_test.py')]),
]
results=[]
for name,command in steps:
    if name in ('build','ctest') and any(r['returncode'] for r in results if r['name'] in ('configure','build')):
        results.append({'name':name,'not_run':'configure/build failed'})
        continue
    start=time.monotonic()
    with (out/(name+'.log')).open('wb') as log:
        try:
            result=subprocess.run(command,cwd=source,stdout=log,stderr=subprocess.STDOUT,timeout=300)
            rc=result.returncode
        except subprocess.TimeoutExpired:
            rc=124
    results.append({'name':name,'command':command,'returncode':rc,'elapsed_seconds':time.monotonic()-start,'log':str(out/(name+'.log'))})
    (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results[-1]),flush=True)
report={'source_pin':'94ead07a45af7cb5dd33a2a708683ec2ba4411b6','results':results,'passed':all(r.get('returncode',1)==0 for r in results),'excluded':{'legacy_TLI_targets':'No legacy TLI/Boost dependency setup; legacy implementation unchanged and performance smoke prohibited in this phase','timed_benchmarks':'Not run'},'windows_failures':'Previous native Windows and one real_dataset failure retained in prior reports; this is a separate Linux run','script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
raise SystemExit(0 if report['passed'] else 1)

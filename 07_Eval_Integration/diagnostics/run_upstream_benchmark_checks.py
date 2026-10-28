"""Record the latest benchmark's own dependency-free core regression checks."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root/'results/upstream_update_audit_20261018'
source = root/'upstream_snapshots/benchmark_003a55bf'
build = out/'build'
commands = [
    ['/home/noctis/.local/bin/cmake', '-S', str(source), '-B', str(build),
     '-DBUILD_LEGACY_BENCHMARK=OFF', '-DCMAKE_BUILD_TYPE=Release'],
    ['/home/noctis/.local/bin/cmake', '--build', str(build), '--parallel', '2'],
    ['/home/noctis/.local/bin/ctest', '--test-dir', str(build), '--output-on-failure'],
]
assert not (out/'benchmark_regression.json').exists()
records = []
with (out/'benchmark_regression.log').open('x', encoding='utf-8') as log:
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True, timeout=300)
        log.write('$ '+' '.join(command)+'\n'+result.stdout+result.stderr); log.flush()
        records.append({'command': command, 'rc': result.returncode})
        print(result.stdout, end='', flush=True)
        if result.returncode: print(result.stderr, flush=True); break
status = 'passed' if len(records)==3 and all(r['rc']==0 for r in records) else 'failed'
(out/'benchmark_regression.json').write_text(json.dumps({
    'status': status, 'commands': records,
    'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'log_sha256': hashlib.sha256((out/'benchmark_regression.log').read_bytes()).hexdigest(),
    'scope': 'upstream unified runner C++ checks and Python suite; legacy TLI excluded'
}, indent=2)+'\n', encoding='utf-8')
assert status=='passed'

"""Sequential acceptance and confirmation pipeline; never overlaps timing jobs."""
from pathlib import Path
import json
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
TOOLS=ROOT/'tools'
BUILDS=ROOT/'builds'

def execute(script,name,*extra):
    subprocess.run([sys.executable,str(TOOLS/script),'--output',str(BUILDS/name),*extra],check=True)

def main():
    execute('run_bm_tx.py','bm_tx_final_sanitizer_20261030','--suite','full','--sanitize')
    report=json.loads((BUILDS/'bm_tx_final_sanitizer_20261030/report.json').read_text())
    if len(report['cells'])!=32 or any(v['terminal_ratio']>1.05 or v['maintenance_peak_ratio']>1.10 for v in report['cells'].values()):
        raise RuntimeError('Space gate failed; formal timing blocked')
    execute('run_bm_tx_performance.py','bm_tx_performance_20261030')
    execute('run_bm_tx_query.py','bm_tx_query_bm0_20261030','--control','bm0')
    execute('run_bm_tx_query.py','bm_tx_query_bm1_20261030','--control','bm1')
    execute('run_bm_tx_performance.py','bm_tx_point_20261030','--point-only','--cells',
            'words_95_5_uniform_growth,urls_80_20_hotspot_growth,words_indep_80_20_uniform_growth,words_100k_95_5_uniform_replacement')
    print('All registered acceptance/confirmation runs complete',flush=True)

if __name__=='__main__':main()

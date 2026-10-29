"""Rebuild the BM acceptance gate from committed sources and pinned local dependencies.

Linux/WSL only. Uses a fresh directory; existing inputs/dependencies are linked
read-only by convention and their hashes/commits are checked by the real runner.
"""
from pathlib import Path
import io
import subprocess
import sys
import tarfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    target=ROOT/'builds/bm_tx_clean_20261030'
    target.mkdir(exist_ok=False)
    revision='10e0494'
    paths=['07_Eval_Integration/diagnostics','tools/run_bm_tx.py','docs/PROTOCOL_BM_TX_20261030.md',
           '02_Code_and_Experiments/validation_e25_20260918/static_physical_index.hpp']
    archive=subprocess.check_output(['git','-C',str(ROOT),'archive',revision,*paths])
    with tarfile.open(fileobj=io.BytesIO(archive)) as source:
        source.extractall(target,filter='data')
    for name in ('upstream_snapshots','results'):
        (target/'07_Eval_Integration'/name).symlink_to(ROOT/'07_Eval_Integration'/name,target_is_directory=True)
    subprocess.run([sys.executable,str(target/'tools/run_bm_tx.py'),'--suite','full',
                    '--output',str(ROOT/'builds/bm_tx_clean_acceptance_20261030')],check=True)
    print('Clean committed-source acceptance completed at '+revision,flush=True)

if __name__=='__main__':main()

#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
cd /mnt/d/StringLearnedIndex_Workspace
rm -f tools/dynamic_smoke.sh tools/_fixrun.py
exec python3 -B 07_Eval_Integration/diagnostics/run_bm_dynamic_entry.py --output "${1:?output name}"

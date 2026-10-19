#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
cd /mnt/d/StringLearnedIndex_Workspace
rm -f tools/dynread_smoke.sh tools/_fixv.py tools/_dyn_report.py
exec python3 -B 07_Eval_Integration/diagnostics/run_bm_dynamic_read.py --output "${1:?output name}"

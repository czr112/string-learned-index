#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
cd /mnt/d/StringLearnedIndex_Workspace
exec python3 -B 07_Eval_Integration/diagnostics/run_bm_space_gen.py --output "${1:?output name}"

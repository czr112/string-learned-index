#!/bin/bash
# Launcher for the C2 short-key entry round (non-login WSL shell keeps /usr/bin on PATH).
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
cd /mnt/d/StringLearnedIndex_Workspace
exec python3 -B 07_Eval_Integration/diagnostics/run_shortkey_entry.py --output "${1:?output name}"

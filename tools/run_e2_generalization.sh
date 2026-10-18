#!/bin/bash
# Launcher for the E2 round: a non-login WSL shell has no ~/.local/bin on PATH, and that is
# where this machine's cmake 3.30.5 lives. Kept as the documented reproduction entry point.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
cd /mnt/d/StringLearnedIndex_Workspace
exec python3 -B 07_Eval_Integration/diagnostics/run_e2_generalization.py --output "${1:?output name}"

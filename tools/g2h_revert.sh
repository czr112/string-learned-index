#!/bin/bash
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
git -C "$W" checkout e28dd74 -- "$D/bm_n1_index.hpp" "$D/bm_split_index.hpp"
bash "$W/tools/n3_gate.sh" 2>&1 | tail -2

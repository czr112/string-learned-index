#!/bin/bash
# RETIRED (2026-10-25): historical 100k v3 runner (2026-10-24). Three hazards when re-run:
#   * line 8 rm -rf's the COMMITTED results directory bm_audited_100k_v3_20261025;
#   * the timing loop truncates cells/*.jsonl inside the reused build dir builds/s2_20261025;
#   * it reuses builds/s2_20261025/pure without any identity check.
# Its committed evidence stays bound to its manifest. Reproducibility of the trace
# generation is verified WITHOUT any overwrite by tools/verify_bm_100k_repro.py.
# See tools/RETIRED.md. Do not execute.
exit 9
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
# 100k coverage: regenerate the 100k traces with the paired generator, then 8 blocks on 8 cells
# and print the aggregated table. The generator is invoked with explicit arguments; it is never
# rewritten in place, so a clean checkout reproduces the same-hash inputs.
rm -rf "$R/results/bm_audited_100k_v3_20261025"

#!/usr/bin/env bash
# matrix4s: the strengthened-protocol confirmation for matrix4 -- mixed corpus, 15 repeats
# (same workload schema, repeats overridden), all eight arms in ONE invocation.
set -u
cd /mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration

MODEL=/mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/Learned-Index-Model/src/experiments
out=results/matrix4s_mixed
if [ -e "$out" ]; then echo "REFUSED: $out already exists" >&2; exit 2; fi
mkdir -p logs
echo "=== matrix4s mixed $(date +%H:%M:%S) ==="
python3 learned-index/scripts/benchmark.py \
  --dataset "$(pwd)/datasets/mixed.keys" \
  --workload "$(pwd)/workload_repeats15.json" \
  --implementation "$(pwd)/adapters/our_index" \
  --implementation "$(pwd)/adapters/our_index_lits" \
  --implementation "$(pwd)/adapters/art" \
  --implementation "$(pwd)/adapters/hot" \
  --implementation "$MODEL/block-plain" \
  --implementation "$MODEL/block-hash" \
  --implementation "$MODEL/block-residual" \
  --implementation "$MODEL/block-residual-delta" \
  --output "$(pwd)/$out" \
  > "logs/matrix4s_mixed.log" 2>&1
rc=$?
echo "=== matrix4s mixed rc=$rc $(date +%H:%M:%S) ==="
if [ "$rc" != "0" ]; then tail -40 logs/matrix4s_mixed.log >&2; exit "$rc"; fi
#!/usr/bin/env bash
# matrix4 smoke: our baseline + partner block-residual-delta through the same evaluator.
set -u
cd /mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration
IMPL_BLOCK=/mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/Learned-Index-Model/src/experiments/block-residual-delta
out=results/matrix4_smoke_mixed
if [ -e "$out" ]; then echo "REFUSED: $out exists"; exit 2; fi
mkdir -p logs
python3 learned-index/scripts/benchmark.py \
  --dataset "$(pwd)/datasets/mixed.keys" \
  --implementation "$(pwd)/adapters/our_index" \
  --implementation "$IMPL_BLOCK" \
  --output "$(pwd)/$out" \
  > logs/smoke_matrix4.log 2>&1
rc=$?
echo "rc=$rc"
tail -6 logs/smoke_matrix4.log
exit $rc
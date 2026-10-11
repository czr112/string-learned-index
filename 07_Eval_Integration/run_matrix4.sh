#!/usr/bin/env bash
# matrix4: partner BlockLearn arms (4 ablations) + our arms + ART/HOT, one invocation per
# corpus so every arm sees the same process blocks and the same machine state.
#
#   arms: ours-context16 | ours-lits | art-libart | hot |
#         block-plain | block-hash | block-residual | block-residual-delta
#
# Output directories are new; existing matrices (matrix1/2/3, smoke) are never rewritten.
set -u
cd /mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration

MODEL=/mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/Learned-Index-Model/src/experiments
IMPL_OURS=adapters/our_index
IMPL_LITS=adapters/our_index_lits
IMPL_ART=adapters/art
IMPL_HOT=adapters/hot
IMPL_BP=$MODEL/block-plain
IMPL_BH=$MODEL/block-hash
IMPL_BR=$MODEL/block-residual
IMPL_BRD=$MODEL/block-residual-delta

mkdir -p logs
for corpus in mixed skew tranco; do
  out="results/matrix4_${corpus}"
  if [ -e "$out" ]; then
    echo "REFUSED: $out already exists" >&2
    exit 2
  fi
  echo "=== matrix4 $corpus $(date +%H:%M:%S) ==="
  python3 learned-index/scripts/benchmark.py \
    --dataset "$(pwd)/datasets/${corpus}.keys" \
    --implementation "$(pwd)/$IMPL_OURS" \
    --implementation "$(pwd)/$IMPL_LITS" \
    --implementation "$(pwd)/$IMPL_ART" \
    --implementation "$(pwd)/$IMPL_HOT" \
    --implementation "$IMPL_BP" \
    --implementation "$IMPL_BH" \
    --implementation "$IMPL_BR" \
    --implementation "$IMPL_BRD" \
    --output "$(pwd)/$out" \
    > "logs/matrix4_${corpus}.log" 2>&1
  rc=$?
  echo "=== matrix4 $corpus rc=$rc $(date +%H:%M:%S) ==="
  if [ "$rc" != "0" ]; then
    echo "FAILED: see logs/matrix4_${corpus}.log" >&2
    tail -40 "logs/matrix4_${corpus}.log" >&2
    exit "$rc"
  fi
done
echo "matrix4 done"
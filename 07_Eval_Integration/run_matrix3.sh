#!/usr/bin/env bash
# matrix3: the external four-way static point-query matrix, re-run at the current index
# identity (index.hpp f573480d...) with TWO added candidate arms.
#
#   arms: our_index (C off) | our_index_routing (C ON) | our_index_lits (C off) |
#         our_index_lits_routing (C ON) | art | hot
#
# WHY ALL SIX IN ONE INVOCATION. The project's rule is same-round comparability: every arm
# must see the same process blocks, the same machine state and the same workload. Running
# the baseline arms in a separate invocation would compare the candidate against a matrix
# measured on different machine state.
#
# The output directories are NEW; the previous matrices (matrix1_*, matrix2_*) are evidence
# and are never rewritten. A pre-existing output directory aborts the run instead.
set -u
cd "$(dirname "$0")"

MI_D=0
I1=adapters/our_index
I2=adapters/our_index_routing
I3=adapters/our_index_lits
I4=adapters/our_index_lits_routing
I5=adapters/art
I6=adapters/hot

mkdir -p logs

for corpus in mixed skew tranco; do
  out="results/matrix3_${corpus}"
  if [ -e "$out" ]; then
    echo "REFUSED: $out already exists" >&2
    exit 2
  fi

  echo "=== matrix3 $corpus $(date +%H:%M:%S) ==="
  python3 learned-index/scripts/benchmark.py \
    --dataset "$(pwd)/datasets/${corpus}.keys" \
    --implementation "$(pwd)/$I1" \
    --implementation "$(pwd)/$I2" \
    --implementation "$(pwd)/$I3" \
    --implementation "$(pwd)/$I4" \
    --implementation "$(pwd)/$I5" \
    --implementation "$(pwd)/$I6" \
    --output "$(pwd)/$out" \
    > "logs/matrix3_${corpus}.log" 2>&1
  rc=$?
  echo "=== matrix3 $corpus rc=$rc $(date +%H:%M:%S) ==="

  if [ "$rc" != "0" ]; then
    echo "FAILED: see logs/matrix3_${corpus}.log" >&2
    tail -40 "logs/matrix3_${corpus}.log" >&2
    exit "$rc"
  fi
done

echo "matrix3 done"
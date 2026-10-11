#!/usr/bin/env bash
# matrix3s: the same six arms as matrix3, on ONE corpus, with a STRENGTHENED but still
# official protocol -- the entry's own --workload override raises the repeat count from the
# default 3 to 15. The default protocol's per-repeat spread is about +-20%, which swamps a
# candidate effect of 5-15%; this run exists to say whether THAT is the reason matrix3's
# candidate arms disagree with the project's own 5-process A/B.
set -u
cd "$(dirname "$0")"

corpus="${1:-tranco}"
out="results/matrix3s_${corpus}"
if [ -e "$out" ]; then
  echo "REFUSED: $out already exists" >&2
  exit 2
fi

python3 learned-index/scripts/benchmark.py \
  --dataset "$(pwd)/datasets/${corpus}.keys" \
  --implementation "$(pwd)/adapters/our_index" \
  --implementation "$(pwd)/adapters/our_index_routing" \
  --implementation "$(pwd)/adapters/our_index_lits" \
  --implementation "$(pwd)/adapters/our_index_lits_routing" \
  --implementation "$(pwd)/adapters/art" \
  --implementation "$(pwd)/adapters/hot" \
  --workload "$(pwd)/workload_repeats15.json" \
  --output "$(pwd)/$out" \
  > "logs/matrix3s_${corpus}.log" 2>&1
rc=$?
echo "matrix3s $corpus rc=$rc"

if [ "$rc" != "0" ]; then
  tail -30 "logs/matrix3s_${corpus}.log" >&2
fi
exit "$rc"
#!/bin/bash
# R1: four growth cells, three arms, one frozen trace each.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"
D="$R/diagnostics"
FRESH="$W/builds/r1_20261020"
rm -rf "$FRESH"; mkdir -p "$FRESH"
MODEL_COMMIT=$(git -C "$R/upstream_snapshots/model_3894c767" rev-parse HEAD)
CORE_SHA=$(sha256sum "$D/bm_bounded_index.hpp" | cut -d' ' -f1)
ADAPTER_SHA=$(sha256sum "$D/bm_experiment_adapter.hpp" | cut -d' ' -f1)
BUNDLE=$(printf '%s' "$CORE_SHA" | sha256sum | cut -d' ' -f1)
FLAGS=(-pthread -std=c++17 -march=x86-64-v3 -O3
  -I"$W/02_Code_and_Experiments/validation_e25_20260918"
  -I"$R/upstream_snapshots/benchmark_003a55bf/src"
  -I"$R/upstream_snapshots/model_3894c767/src"
  -I"$R/learned-index/src" -I"$R/adapters/art" -I"$R/adapters/our_index" -I"$D"
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src"
  -DBM_MODEL_COMMIT="\"$MODEL_COMMIT\""
  -DBM_CORE_BUNDLE_SHA256="\"$BUNDLE\""
  -DBM_EXPERIMENT_ADAPTER_SHA256="\"$ADAPTER_SHA\"")
MODEL_SRC=("$R/upstream_snapshots/model_3894c767/src/block_model.cpp"
           "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp"
           "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp"
           "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp"
           "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp"
           "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp")
gcc -std=c11 -DNDEBUG -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" \
  -c "$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c" \
  -o "$FRESH/art.o"
g++ "${FLAGS[@]}" "$D/bench_bm_r1_growth.cpp" "${MODEL_SRC[@]}" \
  "$R/adapters/art/art_adapter.cpp" "$R/adapters/our_index/our_index_adapter.cpp" "$FRESH/art.o" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" \
  -o "$FRESH/growth" 2>"$FRESH/build.err" || { echo "BUILD FAILED"; tail -20 "$FRESH/build.err"; exit 1; }
T="$R/results/bm_p3_dynamic_20261020/traces"
for cell in english_words_95_5_growth english_words_80_20_growth \
            memetracker_urls_95_5_growth memetracker_urls_80_20_growth; do
  for arm in bm art ctx16; do
    "$FRESH/growth" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$arm" \
      >"$FRESH/${cell}_${arm}.json" 2>"$FRESH/${cell}_${arm}.err" \
      || { echo "FAILED $cell $arm"; tail -5 "$FRESH/${cell}_${arm}.err"; exit 1; }
    cat "$FRESH/${cell}_${arm}.json"
  done
done
sha256sum "$FRESH/growth"

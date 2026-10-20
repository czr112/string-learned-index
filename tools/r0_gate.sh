#!/bin/bash
# R0: build the unified experiment factory in a fresh tree and run the formal gates.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"
D="$R/diagnostics"
FRESH="$W/builds/r0_20261020"
rm -rf "$FRESH"; mkdir -p "$FRESH"
MODEL_COMMIT=$(git -C "$R/upstream_snapshots/model_3894c767" rev-parse HEAD)
CORE_SHA=$(sha256sum "$D/bm_bounded_index.hpp" | cut -d' ' -f1)
ADAPTER_SHA=$(sha256sum "$D/bm_experiment_adapter.hpp" | cut -d' ' -f1)
BUNDLE=$(printf '%s' "$CORE_SHA" | sha256sum | cut -d' ' -f1)
echo "model_commit=$MODEL_COMMIT core=$CORE_SHA adapter=$ADAPTER_SHA bundle=$BUNDLE"
FLAGS=(-pthread -std=c++17 -march=x86-64-v3 -O2
  -I"$W/02_Code_and_Experiments/validation_e25_20260918"
  -I"$R/upstream_snapshots/benchmark_003a55bf/src"
  -I"$R/upstream_snapshots/model_3894c767/src"
  -I"$R/learned-index/src" -I"$D"
  -DBM_MODEL_COMMIT="\"$MODEL_COMMIT\""
  -DBM_CORE_BUNDLE_SHA256="\"$BUNDLE\""
  -DBM_EXPERIMENT_ADAPTER_SHA256="\"$ADAPTER_SHA\"")
MODEL_SRC=("$R/upstream_snapshots/model_3894c767/src/block_model.cpp"
           "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp"
           "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp"
           "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp"
           "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp"
           "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp")
echo "== formal contract acceptance"
g++ "${FLAGS[@]}" -DACCEPTANCE_FACTORY_HEADER='"bm_experiment_adapter.hpp"' \
  -DACCEPTANCE_FACTORY_SYMBOL=make_bm_experiment_index -DACCEPTANCE_SAMPLE_PARAMETER=lookup \
  "$R/adapters/common/contract_acceptance.cpp" "${MODEL_SRC[@]}" -o "$FRESH/acceptance"
"$FRESH/acceptance" >"$FRESH/acceptance.txt" 2>&1 && echo "acceptance ok" || { echo "acceptance FAILED"; exit 1; }
tail -2 "$FRESH/acceptance.txt"
echo "== corpus gate, three real key types"
for name in english_words memetracker_urls memetracker_phrases; do
  g++ "${FLAGS[@]}" "$D/check_bm_experiment.cpp" "${MODEL_SRC[@]}" \
      "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" -o "$FRESH/check"
  "$FRESH/check" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
      "$R/results/bm_real_20261018/interchange/${name}_queries.bin" base >"$FRESH/${name}.json" 2>"$FRESH/${name}.err" \
      && cat "$FRESH/${name}.json" || { echo "gate FAILED $name"; cat "$FRESH/${name}.err"; exit 1; }
done
echo "== sanitized corpus gate (words)"
g++ "${FLAGS[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$D/check_bm_experiment.cpp" \
  "${MODEL_SRC[@]}" "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" -o "$FRESH/check_san"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$FRESH/check_san" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" \
  "$R/results/bm_real_20261018/interchange/english_words_queries.bin" base >"$FRESH/words_san.json" 2>"$FRESH/words_san.err" \
  && cat "$FRESH/words_san.json" || { echo "sanitized gate FAILED"; cat "$FRESH/words_san.err"; exit 1; }
echo "== artifacts"
sha256sum "$FRESH/acceptance" "$FRESH/check" "$FRESH/check_san"

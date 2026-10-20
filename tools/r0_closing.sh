#!/bin/bash
# R0 closing: differential, zero-allocation find, wrapper cost. Fresh tree, three cells.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"
D="$R/diagnostics"
FRESH="$W/builds/r0_closing_20261020"
rm -rf "$FRESH"; mkdir -p "$FRESH"
MODEL_COMMIT=$(git -C "$R/upstream_snapshots/model_3894c767" rev-parse HEAD)
CORE_SHA=$(sha256sum "$D/bm_bounded_index.hpp" | cut -d' ' -f1)
ADAPTER_SHA=$(sha256sum "$D/bm_experiment_adapter.hpp" | cut -d' ' -f1)
BUNDLE=$(printf '%s' "$CORE_SHA" | sha256sum | cut -d' ' -f1)
FLAGS=(-pthread -std=c++17 -march=x86-64-v3 -O3
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
g++ "${FLAGS[@]}" "$D/check_bm_r0_closing.cpp" "${MODEL_SRC[@]}" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" -o "$FRESH/closing"
for cell in english_words memetracker_urls memetracker_phrases; do
  for p in 0 1 2 3 4; do
    "$FRESH/closing" "$R/results/bm_real_20261018/interchange/${cell}_rows.bin" \
      "$R/results/bm_real_20261018/interchange/${cell}_queries.bin" "$p" >"$FRESH/${cell}_$p.txt" 2>"$FRESH/${cell}_$p.err"
    grep '"phase":"gate"' "$FRESH/${cell}_$p.txt" | sed "s/^/{/;s/^}$//" >/dev/null 2>&1 || true
  done
  echo "== $cell"
  grep '"phase":"gate"' "$FRESH/${cell}_0.txt" || cat "$FRESH/${cell}_0.err"
done
sha256sum "$FRESH/closing"

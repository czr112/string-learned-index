#!/bin/bash
# Priority 1: real identities + bounded build + formal factory acceptance for the candidate.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/p1_factory_20261023"; rm -rf "$F"; mkdir -p "$F"
MODEL_COMMIT=$(git -C "$R/upstream_snapshots/model_3894c767" rev-parse HEAD)
CORE_SHA=$(sha256sum "$D/bm_n1_page_core.hpp" | cut -d' ' -f1)
BUNDLE=$(printf '%s' "$CORE_SHA" | sha256sum | cut -d' ' -f1)
ADAPTER_SHA=$(sha256sum "$D/bm_split_adapter.hpp" | cut -d' ' -f1)
echo "model=$MODEL_COMMIT core=$CORE_SHA adapter=$ADAPTER_SHA bundle=$BUNDLE"
FLAGS=(-pthread -std=c++17 -march=x86-64-v3 -O2
  -I"$W/02_Code_and_Experiments/validation_e25_20260918"
  -I"$R/upstream_snapshots/benchmark_003a55bf/src"
  -I"$R/upstream_snapshots/model_3894c767/src"
  -I"$R/learned-index/src" -I"$D"
  -DBM_MODEL_COMMIT="\"$MODEL_COMMIT\""
  -DBM_SPLIT_CORE_SHA256="\"$BUNDLE\""
  -DBM_SPLIT_ADAPTER_SHA256="\"$ADAPTER_SHA\"")
MODEL_SRC=("$R/upstream_snapshots/model_3894c767/src/block_model.cpp"
           "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp"
           "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp"
           "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp"
           "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp"
           "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp")
echo "== formal contract acceptance through the candidate factory"
g++ "${FLAGS[@]}" -DACCEPTANCE_FACTORY_HEADER='"bm_split_adapter.hpp"' \
  -DACCEPTANCE_FACTORY_SYMBOL=make_bm_split_experiment_index \
  "$R/adapters/common/contract_acceptance.cpp" "${MODEL_SRC[@]}" -o "$F/acceptance" 2>"$F/acc.err" \
  || { echo BUILD FAILED; tail -15 "$F/acc.err"; exit 1; }
"$F/acceptance" >"$F/acceptance.txt" 2>&1 || { echo "ACCEPTANCE FAILED"; tail -5 "$F/acceptance.txt"; exit 1; }
tail -2 "$F/acceptance.txt"
echo "== corpus + zero-allocation gate through the candidate factory"
g++ "${FLAGS[@]}" "$D/gate_bm_split_factory.cpp" "${MODEL_SRC[@]}" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" -o "$F/gate" 2>"$F/gate.err" \
  || { echo BUILD FAILED; tail -15 "$F/gate.err"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  "$F/gate" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
            "$R/results/bm_real_20261018/interchange/${name}_queries.bin" >"$F/${name}.json" 2>"$F/${name}.err" \
    || { echo "GATE FAILED $name"; head -5 "$F/${name}.err"; exit 1; }
  cat "$F/${name}.json"
done
echo "== artifacts"
sha256sum "$F/acceptance" "$F/gate"

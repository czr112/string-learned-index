#!/bin/bash
# Priority 4: 8 process blocks x 4 arms on the audited traces, whole-segment timing.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace; R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/p4_20261024"; rm -rf "$F"; mkdir -p "$F"
python3 "$W/tools/_p4_patch.py" && rm -f "$W/tools/_p4_patch.py"
gcc -std=c11 -DNDEBUG -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" \
  -c "$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src/art.c" -o "$F/art.o"
g++ -pthread -std=c++17 -march=x86-64-v3 -O3 \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918" \
  -I"$R/upstream_snapshots/benchmark_003a55bf/src" \
  -I"$R/upstream_snapshots/model_3894c767/src" \
  -I"$R/learned-index/src" -I"$R/adapters/art" -I"$R/adapters/our_index" \
  -I"$W/02_Code_and_Experiments/validation_e25_20260918/third_party/vendor/libart/src" -I"$D" \
  -DBM_MODEL_COMMIT='"3894c767"' -DBM_CORE_BUNDLE_SHA256='"p4"' \
  -DBM_EXPERIMENT_ADAPTER_SHA256='"p4"' -DBM_SPLIT_CORE_SHA256='"p4"' -DBM_SPLIT_ADAPTER_SHA256='"p4"' \
  "$D/bench_bm_pure.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/block_model.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/residual_tags.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/query_cost.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/region_planner.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/learned_index.cpp" \
  "$R/upstream_snapshots/model_3894c767/src/benchmark/model_factories.cpp" \
  "$R/adapters/art/art_adapter.cpp" "$R/adapters/our_index/our_index_adapter.cpp" "$F/art.o" \
  "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp" -o "$F/pure" 2>"$F/build.err" \
  || { echo BUILD FAILED; tail -15 "$F/build.err"; exit 1; }
T="$R/results/bm_audited_traces_20261024/traces"
for cell in words_95_5_uniform_growth words_80_20_uniform_growth \
            words_95_5_hotspot_growth words_80_20_hotspot_growth \
            words_95_5_uniform_replacement words_80_20_uniform_replacement \
            urls_95_5_uniform_growth urls_80_20_uniform_growth; do
  : >"$F/${cell}.jsonl"
  for process in 0 1 2 3 4 5 6 7; do
    taskset -c 0 "$F/pure" "$T/${cell}_rows.bin" "$T/${cell}_ops.bin" "$process" >>"$F/${cell}.jsonl" 2>>"$F/${cell}.err" \
      || { echo "FAILED $cell p$process"; tail -3 "$F/${cell}.err"; exit 1; }
  done
  echo "== $cell done"
done
sha256sum "$F/pure"

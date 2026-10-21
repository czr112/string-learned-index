#!/bin/bash
# N3 gates: four corpora plus sanitizer for the block-rebalance candidate.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
R="$W/07_Eval_Integration"; D="$R/diagnostics"
F="$W/builds/n3_gate_20261021"; rm -rf "$F"; mkdir -p "$F"
FLAGS=(-pthread -std=c++17 -march=x86-64-v3
  -I"$W/02_Code_and_Experiments/validation_e25_20260918"
  -I"$R/upstream_snapshots/benchmark_003a55bf/src"
  -I"$R/upstream_snapshots/model_3894c767/src"
  -I"$R/learned-index/src" -I"$D")
SRC=("$D/gate_bm_n3.cpp" "$R/upstream_snapshots/model_3894c767/src/block_model.cpp"
     "$R/upstream_snapshots/benchmark_003a55bf/src/unified/runner.cpp")
g++ "${FLAGS[@]}" -O2 "${SRC[@]}" -o "$F/gate" 2>"$F/build.err" || { echo BUILD FAILED; tail -20 "$F/build.err"; exit 1; }
for name in english_words memetracker_urls memetracker_phrases; do
  "$F/gate" "$R/results/bm_real_20261018/interchange/${name}_rows.bin" \
            "$R/results/bm_real_20261018/interchange/${name}_queries.bin" >"$F/${name}.json" 2>"$F/${name}.err" \
    || { echo "GATE FAILED $name"; cat "$F/${name}.err" | head -5; exit 1; }
  cat "$F/${name}.json"
done
g++ "${FLAGS[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "${SRC[@]}" \
  -o "$F/gate_san" 2>"$F/build_san.err" || { echo SAN BUILD FAILED; tail -20 "$F/build_san.err"; exit 1; }
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$F/gate_san" "$R/results/bm_real_20261018/interchange/english_words_rows.bin" \
  "$R/results/bm_real_20261018/interchange/english_words_queries.bin" >"$F/words_san.json" 2>"$F/words_san.err" \
  && cat "$F/words_san.json" || { echo "SAN GATE FAILED"; head -20 "$F/words_san.err"; exit 1; }
sha256sum "$F/gate"

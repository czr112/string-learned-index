#!/bin/bash
# Compile smoke test for the C2 entry driver (dev only, produces no evidence).
set -eu
cd /mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration
FLAGS=(-std=c++17 -march=x86-64-v3 -O2
  -I../02_Code_and_Experiments/validation_e25_20260918 -Ilearned-index/src
  -Idiagnostics -Iadapters/art -Iadapters/hot)
LIBS=(results/e2_generalization_20261018/build/art/libart_adapter.a
      results/e2_generalization_20261018/build/hot/libhot_adapter.a)
g++ "${FLAGS[@]}" diagnostics/bench_shortkey_entry.cpp learned-index/src/unified/runner.cpp \
    "${LIBS[@]}" -o /tmp/c2entry
/tmp/c2entry results/static_interface_20261012c/tranco_64k_rows.bin \
            results/e2_generalization_20261018/tranco64_norepl_queries.bin ledger 0 | head -c 1200
echo
/tmp/c2entry results/static_interface_20261012c/tranco_64k_rows.bin \
            results/e2_generalization_20261018/tranco64_norepl_queries.bin phase 0 | head -3

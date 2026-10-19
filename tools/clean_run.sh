#!/bin/bash
# Run one cell with the rebuilt binary and print the capacity fields the round recorded.
set -eu
export PATH="/home/noctis/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH:-}"
W=/mnt/d/StringLearnedIndex_Workspace
FRESH="$W/builds/rebuild_20261019"
R="$W/07_Eval_Integration/results/bm_space_20261019b"
"$FRESH/clean" bm_base \
  "$W/07_Eval_Integration/results/bm_real_20261018/interchange/english_words_rows.bin" \
  "$W/07_Eval_Integration/results/bm_real_20261018/interchange/english_words_queries.bin"

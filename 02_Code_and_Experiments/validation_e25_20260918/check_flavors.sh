#!/usr/bin/env bash
cd /mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/validation_e25_20260918
check() {
  printf "%-52s" "$1"
  if g++ -std=c++17 -O1 -fsyntax-only $2 -march=x86-64-v2 "$3" >/dev/null 2>&1; then echo OK; else echo FAIL; g++ -std=c++17 -O1 -fsyntax-only $2 -march=x86-64-v2 "$3" 2>&1 | head -4; fi
}
check "E25 only (gate flavor)"            "-DE25_DIAG" diag_ov_cost.cpp
check "E25+E18"                            "-DE25_DIAG -DE18_COUNT" diag_ov_cost.cpp
check "E25+E18+E30_TIME"                   "-DE25_DIAG -DE18_COUNT -DE30_TIME" diag_ov_cost.cpp
check "E25+E18+E30_TIME+E30_WRITE"         "-DE25_DIAG -DE18_COUNT -DE30_TIME -DE30_WRITE" diag_ov_cost.cpp
check "shipped (no macros, diag source)"   "" diag_ov_cost.cpp
check "shipped, plain source"              "" check_low_activity.cpp
check "gate flavor, gate source"           "-DE25_DIAG" check_low_activity.cpp

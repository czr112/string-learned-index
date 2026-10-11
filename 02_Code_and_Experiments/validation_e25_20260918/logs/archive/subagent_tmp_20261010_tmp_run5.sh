#!/usr/bin/env bash
cd /mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/validation_e25_20260918
python3 -B tools_build.py diag_low_activity 2>&1 | tail -2
LA_TRACE=1 ./build/diag_low_activity.exe mixed 64 2>&1 | head -12
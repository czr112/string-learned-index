#!/usr/bin/env bash
cd /mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/validation_e25_20260918
echo "source-drift-msg=$(grep -c 'live-count drift' diag_low_activity.cpp)"
echo "bin-drift-msg=$(strings build/diag_low_activity.exe | grep -c 'live-count drift')"
echo "bin-replacement-msg=$(strings build/diag_low_activity.exe | grep -c 'replacement did not add exactly one')"
echo "bin-probe-msg=$(strings build/diag_low_activity.exe | grep -c 'probe_found')"
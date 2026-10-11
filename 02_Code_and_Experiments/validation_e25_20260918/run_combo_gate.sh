#!/usr/bin/env bash
# Combination round gate driver, 2026-10-10.
# Runs the four-arm combination gate over three corpora and both backends, appending every
# run and its exit code to ONE log. Argument 1 is the log name (the script refuses to
# overwrite an existing log), argument 2 is the replacement mode (same|grow).
# rc=3 is the honest NOT EXERCISED outcome: the widened branch never ran on that load, so a
# timing would certify nothing about WIDE there.
set -u
cd "$(dirname "$0")"
log="logs/${1:-combo_gate_v3}.log"
mode="${2:-same}"
if [ -e "$log" ]; then
  echo "REFUSED: $log already exists; pass a new log name" >&2
  exit 2
fi
: > "$log"

run() {
  echo "### check_combo $*" | tee -a "$log"
  python3 tools_build.py run check_combo "$@" >>"$log" 2>&1
  echo "rc=$?" | tee -a "$log"
}

run mixed 131072 '' ctx16 "$mode"
run mixed 131072 '' lits "$mode"
run skew 131072 '' ctx16 "$mode"
run skew 131072 '' lits "$mode"
run tranco 131072 ../validation_e5_20260913/data/tranco_XN6XN.csv ctx16 "$mode"
run tranco 131072 ../validation_e5_20260913/data/tranco_XN6XN.csv lits "$mode"

echo "gate sweep done"
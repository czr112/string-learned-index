#!/usr/bin/env bash
# Inherited-gate re-runs for the TRUNCDIR round, at the sizes the original rounds used.
set -u
cd "$(dirname "$0")"
log="logs/truncdir_inherited_20261011.log"
if [ -e "$log" ]; then echo "REFUSED: $log exists" >&2; exit 2; fi
: > "$log"
run() {
  echo "### $*" | tee -a "$log"
  python3 tools_build.py run "$@" >> "$log" 2>&1
  rc=$?
  echo "rc=$rc" | tee -a "$log"
  return $rc
}
rc_all=0
run check_g1_contract mixed 131072 '' || rc_all=1
run check_compact_routing mixed 131072 '' || rc_all=1
run check_sround mixed 131072 '' || rc_all=1
run check_hole_reuse mixed 131072 '' ctx16 || rc_all=1
echo "inherited sweep rc=$rc_all" | tee -a "$log"
exit $rc_all

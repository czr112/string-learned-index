#!/usr/bin/env bash
# Re-run every correctness gate on all three corpora.
#
# Each gate goes through the strict `run` entry, so a gate whose recorded sources no longer
# match disk REFUSES rather than silently running a stale binary.
#
# THE ARGUMENT FORMAT IS DECIDED HERE, not at the call. Two of these take `KIND:N` as ONE
# argument and the rest take `KIND N`; getting that wrong produced six false "failures" twice
# in a row, which is exactly the kind of noise that hides a real regression.
set -u
cd /mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/validation_e25_20260918 || exit 1

TR="../validation_e5_20260913/data/tranco_XN6XN.csv"

GATES="check_c3 check_e8_hetero check_e9_multiblock check_e11_locate check_e15_plan \
check_e16_probe check_e19_reuse check_e20_r2 check_e25_write check_e26_c2 check_e31 \
check_scan check_scan_contract check_overflow_store check_tag_store check_migrate \
check_freeze check_g1_contract check_lits_model check_lits_fingerprint check_lits_index \
check_i10_commit"

pass=0
fail=0

run_one() {
    local g=$1
    shift
    local out
    out=$(python3 tools_build.py run "$g" "$@" 2>&1)
    local rc=$?
    if [ $rc -eq 0 ]; then
        pass=$((pass + 1))
        echo "PASS  $g $*"
    else
        fail=$((fail + 1))
        echo "FAIL  $g $*  rc=$rc"
        echo "$out" | tail -3
    fi
}

for g in $GATES; do
    case "$g" in
        check_lits_model|check_lits_fingerprint)
            run_one "$g" mixed:131072
            run_one "$g" skew:131072
            run_one "$g" tranco:262144 "$TR"
            ;;
        *)
            run_one "$g" mixed 131072
            run_one "$g" skew 131072
            run_one "$g" tranco 262144 "$TR"
            ;;
    esac
done

echo "=============================================="
echo "gates passed: $pass   failed: $fail"

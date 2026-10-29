# Encoded-page transaction acceptance (2026-10-30)

Execution base: `2f84688`. Scope authorized by the request to continue and complete the
existing work. Tests use the already specified public factory operations/owning scan and
the structural `verify()` seam. No production default changes.

The previous G4 86% observation is discovery evidence only. The implementation now uses
an isolated `make_bm_tx_experiment_index` factory; `make_bm_split_experiment_index` remains
the BM1 control. Shared Page changes are confined to the unused extraction helper,
prefix eligibility accessor and original model parameters for the extracted pages.

## Reproduction and correction

The first run (`builds/bm_tx_red_20261030`) reproduced a ledger-only error after two
inserts: live=size=6146, resident=194223, actual self-reported memory=176759.
`extract_split` grew block vectors geometrically. A subsequent exact-capacity block
split shrank their allocation, while Index recorded only positive deltas. This is
not evidence of lost keys. The candidate accounts for both signs; extracted vectors
also reserve exactly their block count. The green run precedes wider testing.

## Candidate contract

- Build detached lower and upper pages; preserve stable fence bytes, even when their
  original record has been erased; model training uses `(block_size,bytes,segments,error)
  = (1,7,8,1)` exactly as the constructor.
- Use a middle block boundary, require both halves to hold at least 512 existing rows,
  retain the old lower separator, and derive the upper separator from the stable fence.
  Select the destination by comparing the complete key to that separator.
- Prefix-incompatible or otherwise ineligible splits fall back before mutation.
- Prepare pages, map nodes and root before commit. Commit uses no allocations and has
  no recoverable exception path; validate separator/cardinality beforehand.
- Clear the replaced dirty-page entry; publish the root and exact resident delta;
  increment cardinality once. Allow diagnostic attempt counters to change on faults.
- The original target map key survives the Node swap. Account its actual capacity,
  rather than the capacity of a temporary staged copy.

## Gates and order

1. Regression, public contract, complete replay against an independent map, exact
   terminal scans, and root/page invariant verification. Read misses and scans use the
   same byte ordering as the index. Run all existing 24 v3 +8 words_100k cells.
2. New allocation paths: first/middle/last page splits, bytes plus words/URLs/phrases,
   every successful-operation allocation ordinal; after each failure validate the
   original keys/values and exact tracked live bytes, retry, perform exactly 1,000
   insert/delete continuations, destroy with zero live bytes. Run opt and ASan/UBSan
   with leak checking. The preflight and non-failing endpoint are controls, not faults.
3. Same requested-byte ledger for BM1/candidate. Exclude original input and oracle;
   include adapter/index and all operation temporaries. Record terminal, build peak,
   maintenance peak, and destruction. Terminal candidate/BM1 <=1.05 and maintenance
   peak <=1.10; failures remain explicit.
4. Only a candidate passing the preceding gates advances to formal confirmation.
   Discovery performance is allowed in a separate, clearly labelled run.

Formal confirmation uses 16 independent process blocks and balanced arm orders.
Per-metric A/A is required: its process-level log-ratio CI must fit [1/1.05,1.05].
No sample deletion, minimum-time selection or outcome-driven process increases.
The primary new comparison is candidate/BM1, not two ratios to BM0 from different days.
Write target: CI upper <=0.85; pure throughput: CI lower >=1.10. Query upper <=1.05,
with final-state and interleaved find reported separately. Use the existing correct
manifest/live-map query grouping; directory-step counts are not elapsed time.
An unresolved or failed gate blocks adoption and unconditioned scale expansion.

Before formal timing, review tightened UBSan to non-recovering mode and bound ART's
C dependency closure before compilation. Missing the benefit target is recorded as
`target_not_met`, distinct from a confirmed >5% regression. Exact historical mixed90
probes are measured in two direct runs, candidate/BM0 and candidate/BM1. Both validate
all historical groups semantically but time only the frozen primary mixed90 group;
no performance claim is made about the untimed hot/cold subgroups. The old driver
is unchanged unless its explicit experimental compile-time primary-only flag is set.

Every build/run uses `tools/run_bm_tx.py`: fresh directories, pinned dependencies,
actual source closure and adapter hashes, temporary link output and atomic publication,
before-run binary verification and final source/binary drift verification. Preserve all
failed runs. Source files and the runner remain unchanged within a run.

## Stop condition

No new repeated wiring cycles: one implementation and one evidence-driven correction
within the registered budget. If the complete gates reveal a remaining failure, keep
the candidate isolated, record its scope and stop adoption/performance expansion until
that specific condition is resolved. Report actual executed coverage, never the plan.

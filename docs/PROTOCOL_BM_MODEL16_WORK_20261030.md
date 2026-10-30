# Fixed16-segment BlockModel work/space diagnosis

The user authorized continuing optimization. WSL and native timing qualification
did not pass; no elapsed-time benefit may be claimed from this diagnostic.
Use the upstream model commit3894c767 unchanged. This is one fixed parameter
intervention, not a new model algorithm or a segment-budget sweep.

## Intervention and controls

Generate an isolated namespace copy of the same four algorithm headers frozen at
10e0494. Change exactly the two training calls from fit(training,1,7,8,1) to
fit(training,1,7,16,1), and update the model identity string. Keep block_size1,
encoding7, error1, region2048, occupancy15/16, tag/CRC, stable fences, root, arena,
directory lookup and transaction publication unchanged. Require exact source
preimages and exact substitution counts. Original TX8 remains the control.

This differs from the stopped prefix-refinement/cost/adaptive mechanisms: no key
encoding, cluster subdivision, cost objective or routing policy is changed.
Do not try32 segments or another value after seeing these results.

## Deterministic coverage

All32 fixed traces and historical groups, with canonical per-cell row/operation,
pool/final/query hashes. Reuse the public-factory Case semantic/group validation.
Compare TX8 and16 in both dynamic replay and bulk-built terminal snapshot states.
Each concrete replay checks every operation against an independent precomputed
std::map oracle, exact final rows, structure verification, per-page fence arrays
and payload signatures. Report signature agreement as signatures, not a proof
that every unused padding byte is equal.

Use the same C++ requested-byte ledger, excluding pre-existing inputs/oracles.
Record terminal/build/maintenance peaks before diagnostic extraction. Turn off
tracking for diagnostic result buffers; destroy each index with zero tracked bytes
before beginning the next epoch. The core-only ledger is not factory-owned or RSS
space, and does not replace the eventual factory space gate.

Check find without Work for zero allocations on all historical probes; then collect
Work counters separately (which allocate). Record model segment counts, actual
visited payload blocks, exact comparisons, outside/fallbacks and directory steps.
Run opt and nonrecovering ASan/UBSan; deterministic records must agree across builds.
No timers are collected and no concurrent timing/compiler jobs are permitted.

## Nomination, before observing results

For dynamic and snapshot scopes separately:

- Every cell: terminal core requested bytes<=1.05 x TX8, maintenance peak<=1.10,
  zero mismatches/allocating finds/leaks and matching fences/payload signatures.
- At least24/32 primary mixed90 cells: payload blocks<=0.75 x TX8.
- Every primary cell: exact comparisons<=1.05 x TX8, and added directory steps
  <=0.5 per query. Report all subgroup counters, without subgroup latency claims.

If the dynamic scope qualifies, nominate it for a separately registered factory,
new allocation-failure gates and eventual latency/mixed confirmation. If only the
snapshot scope qualifies, nominate only that scope. If neither qualifies, stop this
parameter direction without additional values or target relaxation. Nomination
establishes a work/space opportunity; it is not an elapsed-performance pass.
Context16 remains default and no scale expansion is authorized by these counters.

Build using the shared Run closure/pin/atomic/hash discipline. Preserve generated
headers, all raw counters, failures and exact code/input identities.

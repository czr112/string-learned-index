# Continuous BM execution strategy

Starting point: `cf15c2e`; TX algorithm frozen at `10e0494`. User authorization is
to keep analyzing and implementing until the available quota stops the work.
All phase transitions below use observed evidence, not the number of reports made.
Context16 remains the default. Do not include unrelated workspace changes in commits.

## Current deficit and primary comparison

TX adds transactional encoded-page splitting to BM1 block rebalance. Correctness,
fault rollback and requested bytes passed; new/BM1 write targets passed 9/32 cells,
pure throughput targets 16/32. Historical final-state queries remained AA-unresolved
for both BM0 and BM1 controls. TX pure time versus Context16 has center median 1.414
(includes unresolved cells); four point-only comparisons to ART confirm time ratios
2.065–6.685. Therefore another small write micro-optimization cannot establish a
general baseline win by itself. Read quality and new residual write costs need fresh
evidence after the page-splitting change.

## L0: qualify measurement before using query outcomes

Execute PROTOCOL_BM_QUERY_STABILITY_20261030.md, already registered and reviewed:
two fixed pilot cells, two batch floors, all raw samples, both parity and half-trend
AA tests, source/input-bound selection after completed calibration. If a floor
qualifies, use it once across 32 cells x16 processes, preserving historical query
and final-map SHA. If neither qualifies, do not repeatedly increase the sample size
until a comparison passes, and do not replace elapsed time by CPU time silently.
Retain the environment limitation and continue deterministic diagnosis independently.

Deliverables: completed calibration/selection, environmental diagnostics, formal
query matrix if qualified, and explicit pass/regression/unresolved status. Query
adoption still requires upper <=1.05 and dynamic/snapshot <=1.15 with AA quality.

## L1: establish where query work changed

Execute PROTOCOL_BM_READ_WORK_20261030.md after calibration and before another timing
job. Same 32 cases and valid historical groups, public-factory semantic validation,
concrete core terminal-map agreement, ordinary/sanitizer exact counter equality.
Compare dynamic BM1, dynamic TX and a common final-state snapshot. Distinguish:

- Prefix rejects changing the fraction entering model/payload.
- Model proposal width, outside-window rate and correction-directory work.
- Actual SIMD payload blocks and exact string comparisons, with p50/p95/max.
- Page/block/segment count and root/self-reported storage, reported separately.

Counter differences nominate mechanisms; they do not estimate nanoseconds or prove
why a query regression occurs. An explanation must survive matched-layout or
matched-state intervention before it is presented as causal.

## L2: select one read intervention from L1

Use a timing-qualified environment before claiming a latency gain. Measure root-only,
selected-page and full public find in separate batched passes on the same frozen
terminal index and query order, without a timer on every key. Preserve full answers
and zero-allocation checks separately. Do not subtract two noisy times to manufacture
a small component fraction. Root routing may matter to reads even though it was
only 3%–8% of historical maintenance; these are different denominators.

Only if a component has a reproducible material bound, register one isolated candidate:
for example reducing proposal/payload work if that component dominates, or changing
root representation if its own bound supports it. Previously failed prediction,
payload, local subdivision and adaptive mechanisms stay closed unless the changed
TX state provides a new quantitative bound. At most one implementation correction
after a concrete defect; no indefinite attempts. Keep the TX and BM1 controls frozen.

Require public semantic/byte fixtures, full differential traces, no read allocation,
complete exception rollback for newly allocating paths, requested bytes, paired read
AA and full mixed throughput. Reject an apparently faster query that loses keys,
raises space beyond budget, or transfers excessive cost into writes or scans.

## L3: remeasure the residual write path after TX

The old R/T1 breakdown predates removal of whole-page materialization. Recompute on
current TX before reopening duplicate-search, arena or directory mechanisms. Separate
successful insert/update/erase and fallback reasons; growth and real replacement
must remain distinct. Keep compliant uniform/hotspot traces and independent words.

Important accounting audit: an allocation-capacity delta is not copied traffic.
Current full-block splitting constructs a replacement vector of N+1 Block objects
and N+1 fences; small net resident growth does not bound the existing objects copied.
Collect logical object-copy/allocation counts and split frequency first, then measure
the relevant path separately. Do not describe those counts as actual cache/bus bytes
or claim that historical 1.0–1.3 KB net growth proved copying was negligible.

If remeasured directory allocation/copy work can account for >=15% of current write
time, separately register bounded spare capacity/local directory as one candidate.
The old reserved-capacity attempt had an invalidated reference, so it did not refute
the mechanism. A new attempt must reacquire references after allocation and prepare
all potentially throwing resources before changing the old state. New capacity must
be charged at operation time, including failure rollback and temporary peaks.
If the bound is smaller, close this direction without implementation.

## L4: integrate only the surviving candidate

One explicit factory and real descriptor hashes; no default switch during exploration.
All old controls remain available in the same executable. Repeat all 32 traces and
boundary fixtures; exhaust allocation ordinals at new paths, verify exact requested
bytes rollback, retry plus 1,000 operations and zero destruction bytes in opt and
nonrecovering ASan/UBSan/LSan. Compare terminal <=1.05 and maintenance peak <=1.10
to the registered direct control, and state the denominator clearly.

Use one frozen 16-process confirmation after safety/space gates, with all repeats
and raw records. Write gain >=15%, pure throughput >=10%, no query upper >1.05,
and all required AA checks. Report individual missed targets, confirmed slowdowns
and unresolved cells separately. A limited positive subset is a scoped research
result; it does not authorize a default replacement or erase other failed cells.

## L5: generalization and eventual adoption

Only after the relevant read and mixed gates close: independent trace seeds, sustained
replacement/deletion cycles, scan-heavy workloads, then 250k and million scale.
100k URLs requires a legitimate larger source; never synthesize duplicates and call
it a larger real corpus. Use the same input bytes/factory contract when comparing
ART, Context16 and the external model; label point-only versus scan-inclusive runs.

Default adoption needs consistent external-baseline evidence across intended workloads,
bounded memory/peaks, clean-source rebuild and reproducible artifacts. If the learned
model remains uncompetitive, preserve its demonstrated component/scenario result
and explicitly change the research objective instead of weakening the old gates.

## Continuity and records

Work sequentially within one timing resource; code inspection and planning may
continue while a job runs, but no other compiler/benchmark overlaps it. Keep source
closures immutable during runs. Each completed phase gets a concise report, full
raw evidence index, updated state and a local commit. Reanalyze after each new result;
only record work actually executed as complete. Do not push publicly without new
authorization covering the new commits. If quota interrupts work, preserve running
job identifiers and exact restart/next-step instructions before yielding.

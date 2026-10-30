# Paired relative snapshot estimator: known-truth qualification first

Original absolute-latency gates remain failed, including user-arranged idle trial
ed3f73f. This is a separately labelled relative estimator, not a reinterpretation
or relaxation of those gates. No original sample is pooled. Scope is terminal
read-only snapshots, not dynamic writes or absolute ns stability. Context16 stays
default; dynamic16/fixed8maintenance and closed mechanism families stay closed.

Motivation: [Google Benchmark's user guide](https://google.github.io/benchmark/user_guide.html#random-interleaving)
describes interleaving as a way to reduce system-state-change effects; its
[variance guide](https://google.github.io/benchmark/reducing_variance.html#reducing-variance-in-benchmarks)
lists frequency, scheduling, SMT and cache effects. These support investigating
design, not a diagnosis of this machine. Our ABBA/BAAB choice and thresholds below
are this project's design, not a claim those documents validate this estimator.
The completed idle data's round-level shared projection median is only0.191,
so cancellation is not assumed; independent known-truth qualification is required.

## Fixed design and estimand

Four independently constructed instances A0/A1/B0/B1. In calibration ALL FOUR are
the same TX8 snapshot factory. In confirmation A0/A1 are TX8 and B0/B1 the accepted
Model16 read-only factory421eb36. Budget16/generator/source bytes unchanged.
Every instance receives identical terminal rows and all semantic/group checks.
Release old Case validation indexes before constructing the four test instances.
Rotate allocation order with the existing four-row Williams table by process.

One shared power-of-two loop count with every instance>=1000ms (same32,768,000
lookup cap), existing250ms minimum warmup, no timer/check inside individual find.
Use original frozen native batch/timed helpers via exactd337576 preimage generation
with checked entry rename and explicit successful return0; no tracked source edit.
Windows sameGCC15.2/runtime identities, CPU0/minimum allowed, normal priority,
wall primary; CPU time/cycles diagnostic. No global/environment tuning.

Eight quads/process,4 batches/quad =32 measured batches, same count as old design.
q=0..7: i=q%2, j=(q//2)%2. Pair Ai with Bj. ABBA if(q//4+process)%2==0, otherwise
BAAB. Thus each pair occurs once each way; each replica has8 batches. Record
envelope timestamps, CPU/cycles, count/checksum and all calibration/warmups.

For each quad e_q=mean(log(ns/lookup) for B)-mean(log(ns/lookup) for A).
Average8 e_q within process, then pointwise t interval across independent processes
(df=n-1). Weights(-1/2,+1/2,+1/2,-1/2) cancel constant/linear log drift at equally
spaced positions in ideal ABBA. Actual timing is not guaranteed equally spaced;
we do not use that algebra to bypass empirical quality gates or change weights.

## Qualification and fixed gates

Start compilation/testing only after the prior idle window ends06:17:13UTC;
no second measurement trial in that window. New method is tested under ordinary
foreground conditions, no request to keep the machine idle. No competing project
compiler/timing jobs during its measurement. Per-child cap900s, preserve failures.

First run synthetic analysis checks with independently known ratios, linear log
drift, malformed coverage and df; these are not real benchmark results. Then opt
native public contracts TX8=99,Model16=127, all32 semantic checks, and known-truth
calibration: two original pilot cells,4 fresh processes/cell,8 quads. No Model16
elapsed measurement in calibration, no algorithm comparison. Always both cells.

All checks required in both cells:
- Known-truth B/A CI inside[1/1.025,1.025].
- Replica A1/A0 and B1/B0 CIs each inside[1/1.05,1.05].
- Odd/even and late/early contrasts of e_q (differences of means, exponentiated)
  CIs each inside[1/1.025,1.025]. Raw absolute drift still reported diagnostically.

If it fails, stop this estimator without more batches/processes, another ordering,
another floor or target relaxation. No candidate timing or adoption. If it passes,
freeze complete source/protocol/report/input/affinity/toolchain identity and run
ONE32cell x16process confirmation, with the same replica/stability gates and
pointwise95% relative CIs. This is a new relative result only; old absolute gates
are never marked passed. Confirmation is single serial project job, not an idle
window claim; user can use the computer normally and quality gates still apply.

Relative read target: CI upper<=0.90 in>=24/32 primary mixed90 cells and all32
upper<=1.05, with every quality gate. Otherwise report scoped/undecided/unresolved
cells; no default adoption. No subgroup/scan/mixed-write/ART claims from this test.

Use accepted exact16 generator/preimages and shared NativeRun closure/pin/atomic/
binary/drift discipline; register invoking/imported stats/generator/protocol files.
Before any sampling preserve actual input hashes, registration, all metadata and
known-truth identities; after qualification validate raw-sample hashes and recompute
selection. Preserve all failures/raw records. New policy or source changes require
registration before new measurements, not editing results into a pass.

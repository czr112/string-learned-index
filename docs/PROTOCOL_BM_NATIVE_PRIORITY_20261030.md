# Native thread-priority qualification (separate environment trial)

Prerequisite: native normal-priority calibration d337576 failed. Keep those failures
and do not pool samples. At1000ms each pilot passed3/4 states, while observed maximum
wall/thread-CPU ratios were1.262 and1.240. This supports one scheduling intervention,
not a claim that scheduling caused every failed gate. Existing protocol remains failed.

Change only this executable's own thread priority to THREAD_PRIORITY_ABOVE_NORMAL;
keep process priority class and global settings unchanged. Save/restore the previous
thread setting, verify actual before/after, report it separately from affinity.
No REALTIME/HIGH process priority, CPU shopping, power tuning or other applications.
Reuse original native code/factories and checks, toolchain/runtime identity and
fixed lowest permitted CPU. No algorithm, inputs, groups or estimator change.

One fixed1000ms floor; no250ms trial or longer floor. Same two historical pilot
cells,4 fresh processes each,8 four-state Williams rounds, original calibration
cap and warmup. Preserve every trial/sample. Every state in both cells must satisfy
both original parity and half-trend AA intervals inside[1/1.05,1.05], df=n-1.
Analyze calibration using AA only, never candidate/control ratios. Failure closes
this local scheduling trial; no sample extension or priority escalation.

Only if calibration passes, freeze report/source/protocol/input/toolchain/affinity
and priority identity and execute one16-process32-cell confirmation using unchanged
original TX/BM1 contrasts, query no-regression1.05 and dynamic/snapshot1.15. Keep
pointwise intervals and AA-unresolved classifications; no automatic adoption.
The Model16 snapshot is not in these states and cannot inherit a performance pass.
A later Model16 timing comparison still requires its own fixed scope and controls.

Use shared NativeRun and imported unchanged analyze/frozen_inputs/closure_digest.
The wrapper prints priority metadata before/after the reused original program;
raw logs preserve those lines, parser requires both and confirms restoration.
Bind every imported runner/protocol and actual wrapper in closure. Complete public
contract and all32 semantics first, no concurrent measurement/compiler jobs.
Context16 remains default. Keep failures and source/binary/input drift checks.

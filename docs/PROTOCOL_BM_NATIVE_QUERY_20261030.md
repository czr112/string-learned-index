# Windows-native query qualification

Renewed user authorization permits continuing execution. Algorithm remains TX
`10e0494`, Context16 default. The old automatic-goal record is not used as live
account availability: the account tool reported ordinaryUsageAllowed=true.

## Why a new environment

The registered WSL 250/1000ms pilots failed both AA checks, and directory timing
also failed qualification. This experiment changes execution environment to the
already installed MinGW GCC15.2 Windows-native toolchain, with a thread pinned to
the lowest allowed native processor. It is not another repetition or sample
extension of either WSL run; no old samples are pooled. No program installs,
global power settings, other processes' affinity or priorities are changed.

Use SetThreadAffinityMask on the test's own thread before constructing the case.
Require one processor group; reject unsupported affinity configurations. Record
the native processor mask and processor before/after every batch. Wall time remains
primary. Record GetThreadTimes CPU duration and QueryThreadCycleTime cycles as
diagnostics; cycles must never be converted to elapsed time. Windows context-switch
counts are unavailable here and must not be reported as zero.

## Same data and statistical decision

Keep the historical mixed90 queries/final maps byte-for-byte, the same four states
(BM1/TX dynamic and snapshots), original model parameters, independent replay and
all historical group/rank/live-map checks. Native public contract must pass99/99,
and all32 cases must pass semantic checks before calibration. This does not assert
Windows sanitizer coverage: the frozen algorithm's sanitizer evidence is from WSL.

Use exactly the existing two pilot cells: words_95_5_uniform_growth and
urls_80_20_hotspot_replacement. Run both250ms and1000ms configurations, four fresh
processes per cell, eight measured rounds in a process/round-rotated Williams order.
Calibrate one power-of-two loop count sufficient for all states, capped at32,768,000
lookups. Warm each state for at least250ms; retain warmup aggregate records and all
loop-calibration trials separately, never use them as measured observations.

AA requires both odd/even and late/early-half process-level log-ratio95% t intervals
within[1/1.05,1.05], for every state and both pilot cells. The selected floor is the
shortest passing one, based only on AA. Publish selection only after a successful
completed report and final source/input/toolchain drift checks. Bind selection to
the complete report SHA, source closure, original/generated input hashes and actual
raw-AA reanalysis. If none passes, no formal comparisons or additional samples.

If qualified, one new16-process confirmation across all32 cells, same frozen
protocol/analysis/code/toolchain. Candidate/control full-find upper<=1.05 with both
AA checks passing; dynamic/snapshot upper<=1.15 reported separately. Confirm
regression only with lower>the corresponding threshold and valid AA. Otherwise
report unresolved/undecided. Intervals are pointwise, not simultaneous adoption
guarantees. The native experiment may establish native-target evidence; it cannot
retroactively erase WSL-specific failures or close the separate write/space gates.

## Reproducibility and APIs

Reuse Run's source scans, pins, binary hashes, immutable directories and final
drift checks. The native adapter uses .exe final paths and .partial.exe link outputs,
with atomic renames before publication. Bind the local compiler and available
cc1plus/collect2/linker/assembler/runtime binaries; record their version and paths.
Stage registered GCC runtime DLLs beside the executables with atomic publication,
verify them before launches and at completion, and verify the modules' reported
loaded paths/hashes for every native semantic and measurement process. No compiler/benchmark
jobs overlap. Keep native raw data and failures separate from all WSL results.

API sources: [thread affinity](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadaffinitymask),
[thread CPU times](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes),
[thread cycles](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).

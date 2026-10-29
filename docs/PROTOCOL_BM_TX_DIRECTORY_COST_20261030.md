# Current TX block-directory cost diagnosis

Base `fad6e43`, frozen algorithm `10e0494`. The user explicitly renewed continuing
execution. This is L3 diagnosis, not an optimization or an adoption experiment.

## Scope and measurement

The full-block branch in Page::try_insert replaces both the Block and Fence
vectors. Measure that complete branch, including tail preparation, allocation,
copy, publication, old-directory destruction and destination selection. This is an
optimistic bound on eliminating the branch, not the achievable saving of reserving
capacity. Net resident growth is never called copied traffic.

Generate a diagnostic namespace copy of exactly four headers from `10e0494`:
Page, root, original N1 Index and TX Index. Require their current bytes equal the
committed preimage. Rename namespaces/includes mechanically and add one RAII
timer at the unique full-block branch. No stored fields, algorithm expressions or
production files change. A copy of the existing allocation wrapper additionally
counts cumulative requested bytes; use it for both controls. Compile original and
diagnostic Index types together.
Record original/generated SHA, generator, helper, driver, dependency closure and
binary identity with the existing Run builder and immutable output directories.

The timer records event count, the logical N+1 Block and Fence objects constructed,
their sizeof-based bytes, requested allocation calls/bytes during the branch, and
wall duration. These object bytes are not physical memory-bus/cache traffic. A
single-threaded requested-byte wrapper is used for both controls; results apply to
that diagnostic allocation environment.

For every replay, validate answers against an independent std::map outside timers,
compare the full terminal map, verify structure, checksum every operation result,
and destroy with zero tracked bytes. Run original/diagnostic on all32 historical
traces in opt and nonrecovering ASan/UBSan. Compare deterministic event, allocation,
shape and memory fields across builds, excluding time. The diagnostic event count
must equal the actual Page full-block-split counter delta. Source row/ops and
historical final-map hashes are verified per cell before and after execution.

## Cost pilot and decision fixed before timing

Only after those semantic checks pass, run four fixed cases:
words/URLs x 80/20 x uniform/hotspot growth. Four fresh process blocks per case;
four labels (original, instrumented, original_AA, instrumented_AA) in Williams
order, two rounds, eight fresh trace replays per label observation. No additional
warmup trace is discarded: retain every timed replay in those observations. Preserve every
sample; this pilot is a cost bound, not a formal performance win. Oracle/checksum
validation and index build/destruction are outside the write timers. Find/scan
remain in the trace; writes have per-operation timers in both controls. The
directory branch timer exists only in the diagnostic implementation.

Aggregate log write durations within process, then pointwise t intervals with
df=n-1. Both implementations' AA intervals must fit [1/1.05,1.05]. The paired
instrumented/original write-time interval must also fit this range to qualify the
instrumentation. Within a process, branch share is the ratio of summed directory
nanoseconds to summed write nanoseconds across all instrumented observations (a
write-time-weighted share). Average those four process shares and use a pointwise
t interval (not a simultaneous or causal estimate).

A bounded-reserve candidate may be separately registered only if at least two
fixed cases meet the quality checks AND the branch-share CI lower bound >=30%.
The 30% headroom avoids assuming that the whole branch can be removed; a roughly
half-sized branch saving would then be worth investigating for the 15% write gate.
It is not a promise of such savings. Otherwise retain the cost evidence and stop
this candidate, without adding processes or relaxing the quality requirements.

Timing remains diagnostic: prior query AA failures are not closed, Context16 stays
default, and no >100k expansion or production change follows this pilot alone.

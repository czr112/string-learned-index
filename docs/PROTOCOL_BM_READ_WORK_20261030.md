# Read work diagnosis after TX acceptance

This deterministic diagnostic does not change the candidate or produce latency
claims. It follows the user's continuing authorization. Run only after the query
calibration finishes, before any formal timing starts; never overlap timing jobs.

Use the same 32 frozen traces, historical groups and final states. The inherited
Case constructor verifies semantics through public BM1/TX dynamic/snapshot factory
operations, source ranks, hot bounds, live membership and exact terminal maps.
Replay the same mutating operations through the two concrete public Index APIs,
verify structure, and compare the full owning scan against the validated terminal
map. Also build one common final-state snapshot (both factories share construction).

For each validated group, collect the existing find_base Work counters: queries,
prefix rejects, model calls, proposed blocks, boundary checks, outside/fallbacks,
directory steps, visited payload blocks and exact string comparisons. Verify every
answer against the frozen query oracle. Record visited-block percentiles including
zero for prefix rejects; report page/block/segment counts and self-reported bytes
separately. Counter instrumentation allocates; this is not a zero-allocation or
elapsed-time experiment, and counter ratios are not cost fractions or causal proof.

Hypotheses to distinguish: TX changes model-window quality, prefix rejection,
payload exploration, or only routing/topology. The diagnosis can reject simple
explanations and nominate a focused timing experiment; it cannot by itself close
the 5% query gate or authorize production adoption. Do not reopen the previously
failed prediction/payload/arena/duplicate-search mechanisms without a new measured
bound and separate registration.

Build with the shared immutable Run builder and nonrecovering sanitizer companion;
bind runner/protocol/source closure and all input hashes. All 32 cases must pass
ordinary and sanitizer diagnostic runs. Keep raw per-group records and failures.

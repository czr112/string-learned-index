# Query stability calibration and confirmation

Execution base: `cf15c2e`. The user authorized continuous implementation and analysis.
This is a new measurement registration; no prior samples are pooled or discarded.
Production remains Context16. The TX algorithm remains at `10e0494`.

## Question and fixed inputs

Can longer batches and eight within-process balanced repeats provide reliable A/A
on this machine? Keep the historical mixed90 query bytes, final states, factory
parameters and byte ordering unchanged. Reuse all semantic/group validations from
the frozen query harness. Direct control is BM1; four states are BM1/TX dynamic and
their final-state bulk-built snapshots. No new key grouping is introduced.

## Calibration, before inspecting performance contrasts

- Fixed CPU: smallest CPU in the process's allowed affinity set, as before. Record
  affinity, host/kernel/CPU information and load; no CPU shopping or global tuning.
- Fixed calibration cells: `words_95_5_uniform_growth` and
  `urls_80_20_hotspot_replacement`, chosen for distinct key shape and semantics.
- Run both minimum-batch configurations, 250 ms and 1000 ms, each in four fresh
  processes per cell. Always execute both configurations; keep all observations.
- In each process, calibrate one power-of-two loop count sufficient for every
  state, capped at 32,768,000 lookups per batch. Warm each state for at least
  250 ms, then take eight measured rounds. Rotate a four-row Williams order by
  process and round, covering each position twice per state within each process.
- Record wall and thread CPU time, voluntary/involuntary context switches,
  minor/major faults and CPU before/after each batch. Wall time remains primary.
  CPU time is diagnostic; it cannot silently replace elapsed-time acceptance.
- For each state, test both odd/even round means and late/early half means using
  process-level log ratios and pointwise t intervals (`df=n-1`). Both A/A intervals
  must fit `[1/1.05,1.05]`. Every calibration cell and state must pass.
- Select the shortest passing configuration using only these A/A results. The
  calibration report does not calculate candidate/control performance contrasts.
  If neither passes, do not run formal performance or relax this rule; diagnose
  the retained scheduling/time records and pursue only separately labelled work.

## One formal confirmation if calibration succeeds

Freeze the chosen batch floor, code and analysis hashes, then execute 16 fresh
process blocks for every existing cell (24 v3 +8 words_100k), once. Semantic checks
must pass first. Apply the same eight-round schedule and both A/A checks. For each
contrast, average log per-lookup times within process before the t interval across
processes. Query no-regression requires CI upper <=1.05 and both compared states'
A/A quality. Confirm regression only if CI lower >1.05 and A/A passes; otherwise
retain `undecided` or `measurement_unresolved`. Dynamic/snapshot budget <=1.15 is
reported separately. Intervals are pointwise, not a simultaneous adoption proof.

Keep the old unresolved results. A new passing read gate alone cannot override
unmet write/throughput targets or justify default replacement. Any stable read
regression is followed by a separately registered root/page/block diagnosis.
No outcome-driven sample extension, minima, outlier removal or warm-run selection.

## Build and artifact discipline

Use the shared `run_bm_tx.Run` builder: pinned dependencies, compiler dependency
closure hashes captured before building, `.partial` output and atomic publication,
binary hash verification before every run, and source/binary drift checks at end.
Register the invoking runner and this protocol in the closure. Fresh output
directories only. The runner verifies original row/operation hashes and the
historical query/final-state hashes. Preserve commands, environment, calibration
trials, per-batch raw records, all failed runs and the selection record.

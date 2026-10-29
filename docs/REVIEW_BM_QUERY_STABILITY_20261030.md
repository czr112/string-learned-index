# Query stability review

Fixed point `cf15c2e`, initial registration `d971d0b`; follow-up fixes reviewed before
any compilation or measurement. Spec source: PROTOCOL_BM_QUERY_STABILITY_20261030.md.

## Standards

No documented-rule blocker. One nonblocking primitive-obsession judgment: calibration
used a positional five-integer array. Replaced it with named CalibrationSample fields
and shared emit_batch serialization. Reviewer confirmed the fix.

## Spec

Two findings, both corrected and rechecked before measurements:

- A selection could be written before successful completion, and confirmation did
  not prove that calibration completed. Publish only after finish; bind the complete
  calibration report SHA. Confirmation verifies completion, all cells/configurations,
  unchanged inputs/source closure, reaggregates raw blocks and validates the shortest
  passing A/A-only choice.
- Calibration trials discarded scheduling diagnostics. Retain full StableBatch and
  serialize CPU time, wall time, context switches, faults and CPU identifiers for
  calibration trials as well as measured batches.

Both axes reported no remaining blocker. Reviewers did not run compilers or measurements.

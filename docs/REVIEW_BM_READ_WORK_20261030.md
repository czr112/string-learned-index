# Deterministic read-work review

Fixed point `2cae5cc`, implementation `ffdf301`, follow-up reviewed before execution.
Spec: PROTOCOL_BM_READ_WORK_20261030.md. Both review agents were read-only and did
not compile or measure while the independent stability calibration was running.

## Standards

One documented coverage finding: matching 32 case entries against a global set of
input hashes did not bind each cell label to its own trace. Fixed: exact canonical
label set, five-path arity, row/operation hashes from original manifests and
pool/final/query hashes from the historical case, additionally matching calibration
input hashes. The reviewer confirmed no remaining blocker.

## Spec

The same trace-substitution gap violated the requirement to cover every frozen cell.
The per-cell identity fix closes it. Concrete replay/full terminal comparison,
existing Work counters, zero-inclusive percentiles, and opt/sanitizer equality
otherwise match the diagnostic protocol. Reviewer confirmed no remaining finding.

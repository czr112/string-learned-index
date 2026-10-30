# Own-thread priority qualification review

Spec: PROTOCOL_BM_NATIVE_PRIORITY_20261030.md. Initial fixed range
2bee00d...cf40463, narrow followups cf40463...597c4a6 and597c4a6...0b9df65.
Both reviewers read only; no reviewer ran a build or experiment.

## Standards

Initial inherited-protocol binding gap closed by adding native.PROTOCOL and
old_statistics.PROTOCOL to run.sources. Ordinary renamed-entry fallthrough
finding closed with exact checked explicit success return. No additional
actionable smell findings; latest verdict clear.

## Spec

Nested main macros initially blocked compilation. Isolated d337576 preimage
generation now checks one entry-name substitution and one explicit return0,
preserving main semantics without changing tracked original. Generation SHA
is retained. Both inherited protocols are bound. AA-only calibration, fixed
counts/thresholds, raw sample binding, own-thread restoration and separation
from Model16 remain in place. Latest verdict clear.

Compile attempt1 failed on macro collision; attempt2 built before explicit-return
correction and was not run. Actual qualification will compile corrected0b9df65
anew. Those attempts are retained, not counted as timing samples.

Standards0 outstanding; Spec0 outstanding. No latency result from this review.

# Paired relative snapshot review

Fixed rangeed3f73f...deb9d2b. Spec:
PROTOCOL_BM_PAIRED_SNAPSHOT_20261030.md. Reviewers read only; no jobs,
builds, experiments or edits were performed. Compile-only completed and five
synthetic known-answer checks passed before review; neither is real timing data.

## Standards

Clear: accepted adapter/generated source bytes match pins; imported analysis/
protocols/generator, toolchain/runtime and inputs are bound. Generated source,
atomic binaries and raw sample hashes/reanalysis are retained. Synthetic checks
use independently known outcomes. No actionable smell or hard violation.

## Spec

Clear: quad sequencing/weights, process-level aggregation and t degrees of
freedom, all-TX8 known-truth roles, calibration unity test, formal replica/stability
guards, fixed4/16 process counts and raw-selection reanalysis match the protocol.
Relative target aggregation is correct. Old absolute gates remain failed and
unmodified; no default/ART/scan/write claim is created.

Standards0 findings; Spec0 findings. Next actual run is known-truth qualification
under ordinary foreground conditions, after the earlier idle window ended.

# Human-confirmed idle window review

Initial fixed range836802a...39e6c02; followups39e6c02...29fda45 and
29fda45...1779729. Spec: PROTOCOL_BM_NATIVE_IDLE_WINDOW_20261030.md.
Human confirmed the window begins now at05:47:13UTC for at least30minutes;
hard end is06:17:13UTC (local14:17:13). No reviewers ran jobs or edited.

## Standards

No-build provenance gap fixed: identity.json records individual current control
source hashes, frozen compiled closure, verified sources, copied binaries and
staged runtime hashes before first child. Latest verdict clear.

## Spec

Deadline budget initially preceded hashing/JSON work. It now refreshes immediately
at launch, rejects expiry after return, and subprocess timeout stops only the
owned child. Reused identities, runtime paths, inputs/affinity, AA-only calibration
and raw-selection binding are consistent. Latest verdict clear.

Standards0 outstanding; Spec0 outstanding. Future confirmation still needs a
qualified selection and separately confirmed sufficiently long idle window.

# Native query qualification review

Fixed point `959f189`, implementation `1ba8c71`, fixes `47c9028` and `fd2d999`.
Spec: PROTOCOL_BM_NATIVE_QUERY_20261030.md. Reviews completed before compilation
or measurement; agents were read-only.

## Standards

Two binding gaps fixed: register the actual collect2 linker driver, and bind the
runtime DLLs actually used rather than only their originals beside the compiler.
DLLs now stage atomically beside the executables, are checked before each launch
and at finish, and reported loaded paths/hashes are verified for every query
semantic/measurement process and for the public contract process as well.
The shared runtime emitter is dependency-scanned. Reviewer confirmed no blocker.

## Spec

The collect2 omission also broke frozen-toolchain confirmation and was corrected.
Canonical inputs, normalized generated-contract closure, A/A-only selection,
reanalysis, native affinity and explicit absence of native sanitizer claims were
otherwise consistent. The final contract wrapper preserves its original return
status, requires one runtime record, and verifies it with the same DLL checks.
Reviewer confirmed all fixes and no remaining finding.

# Model16 snapshot standalone source closure and clean rebuild

Accepted candidate421eb36 is read-only, functional/space only. Do not change its
source, budget, capabilities or production defaults. No elapsed-time test or scale
expansion. This phase makes that exact candidate independently buildable, without
requiring the dirty workspace, its Git metadata, generated-path setup or datasets.

Export ONLY the two public-contract compilation dependency closures from accepted
bm_model16_snapshot_acceptance_20261030, checking dependency-log hashes against
the archived member manifest and each source against accepted identity closure.
Flatten to original basenames; reject any differing-byte name collision. Preserve
all source bytes, including four generated16 headers and the contract entry.
Include provenance, benchmark license, upstream model/benchmark pins, and accurate
notices (the pinned model snapshot has no top-level license file; do not infer one).
Private local reproducibility artifact only; no upload or public distribution.

Include a standalone Python-standard-library rebuild entry which imports no
workspace modules and calls no Git/network tools. It verifies the full source
manifest, scans actual compiler dependencies, and rejects non-system dependencies
outside the exported directory or missing from the fixed closure. Compile from
relative names, with no original-workspace include paths. Remove ambient CPATH/
CPLUS_INCLUDE_PATH/C_INCLUDE_PATH/LIBRARY_PATH in child compiler environment.
System toolchain headers/libraries remain normal dependencies, not exported code.

Bind real relative-path closure SHA, adapter SHA, fixed model commit and generated
header in compile definitions. Record compiler/helper identities, commands, raw
logs, source hashes and binary hashes; publish binary from .partial atomically,
verify before execution and after, and check source drift at completion. Use fresh
build directories and preserve failures. Same atomic/hash discipline as shared
Run, independently implemented in the exported consumer so it has no workspace
dependency. Do not represent a source hash as a timing result.

Run in the exported directory with opt and nonrecovering ASan/UBSan/LSan, both
using the exact public borrowed_contract_acceptance.cpp and actual snapshot factory.
Require actual127/127 on both; absence of original workspace includes is verified
from actual -MM outputs. This is a clean source/contract rebuild, not a repeat of
the full32/fault/space matrix and not a new performance acceptance. Existing32/fault
evidence remains separately bound to accepted source bytes.

No original tracked source rewritten, no dependency version upgrade, no binaries
or datasets delivered. Preserve generated package, both raw build logs and source
archive with per-member SHA. Any closure/build/contract mismatch stops delivery.
The exporter must create source_bundle.tar.gz from ONLY manifest-listed source
files plus manifest.json, stream-verify every archive member, and publish an
external per-member SHA receipt. Keep opt/san logs outside that delivered archive.
Context16 remains default; dynamic16 and fixed8 maintenance remain closed.

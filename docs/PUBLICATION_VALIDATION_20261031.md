# Publication validation, 2026-10-31

This release preserves the original 84 local commits and the five preparation
commits ending at `9593ed8f9a0dbc78ef5d1e44fea4b92ab989f296`. The live GitHub main
was checked through the existing Git HTTPS proxy and remained
`6229ccc1cab196baa92565575a88320f479ec109`, an ancestor 89 commits behind that tip.
No candidate optimization is merged or adopted. Context16 remains the default.

## Linux benchmark regression

The pinned evaluator `94ead07a45af7cb5dd33a2a708683ec2ba4411b6` was checked in
WSL Ubuntu with GCC 13.3.0. Formal execution approvals restored read and execution
access; no system permissions, proxy settings, or installed packages were changed.

CMake Debug configuration, a single-job build, and serial CTest all passed:

- `unified_runner_checks`, `unified_benchmark_checks`, `benchmark_suite_checks`: 3/3.
- Independent `lookup_workload_test.py`: 9/9.
- Independent `real_dataset_test.py`: 6/6.

Legacy TLI/Boost targets were excluded with `BUILD_LEGACY_BENCHMARK=OFF`; their
dependencies were not initialized and this phase does not change their implementation.
Performance smoke runs and timed benchmarks were excluded. These functional results
do not qualify elapsed performance or ART/HOT comparisons. Previous Windows native
failures and the Windows real-dataset error remain recorded in their original reports;
the Linux run does not relabel those earlier results.

Commands, outcomes, raw logs, and script identity are recorded in
`../07_Eval_Integration/results/publication_linux_20261031/`.

## Original workspace and publication audit

The original D workspace remains at `0b0181b` with its local changes intact.
Its ten modified tracked files match the import in `0499fe5` byte for byte.
Nine also match the prepared release tip; README has additional intentional navigation
changes after that import. All 1,538 untracked files are accounted for:

- 1,520 text/source evidence files are archived and individually SHA-256 verified.
- Two navigation documents are committed unchanged.
- Two executables, one 30 MB raw compilation log, six OS metadata files, three
  frozen upstream `.gitignore` files, and one empty probe remain local and backed up.
  The frozen ignore files are repository configuration snapshots, not omitted index code.
- Three local ACL diagnostic files remain in the verified local backup; they are
  machine diagnostics and are not project source or publication material.

The complete 1,548-file preservation backup was verified and original contents and
Git status were rechecked unchanged. The publication audit covered 869 changed regular
files (19,217,036 bytes) and 6,230 archive members before this validation record.
Common credential signatures, executable magic, unsafe archive paths, compiled archive
members, and new files above 10 MiB produced no findings. The largest new file is the
3,340,887-byte historical text archive. This is a limited audit, not a full security proof.

Whitespace checks preserve the raw `hot_native_compile.log` failure evidence exactly
as committed in `9593ed8`; its compiler alignment spaces are the sole explicit exception.
All other paths pass the configured check. Historical evidence is not reformatted.

## Parallel work remains isolated

The three local workspaces are siblings of this publication checkout under
`builds/project_recovery_20261031/`. Their starting points are recorded here for
coordination; ongoing work is subject to separate review before integration.

| Workspace | Working branch | Starting commit |
| --- | --- | --- |
| `route-a-bounded-fence` | `codex/bounded-fence-20261031` | `0f344d14579f701132cc67816d6751529037729f` |
| `route-b-model-cost` | `codex/model-cost-ablation-20261031` | `125caa288312af0182e23ade15946863b1b51fd1` |
| `route-c-hot-baseline` | `codex/hot-baseline-20261031` | `0c2e6024138ebba0bdd0260bb2d5766a6eb67c14` |

Route A includes the rejected ordinal-map matrix and long-delete work evidence.
All recovered C refs remain available in each isolated repository under
`refs/recovered-c/`. No experimental branch is included in this main publication.

After publication, the original dirty D checkout still requires deliberate
reconciliation using the preservation manifest. Do not pull, reset, clean, or overwrite
that directory to synchronize it. Remote delivery is verified separately after push;
this document records validation rather than asserting delivery in advance.

# BM clean-checkout verification (2026-10-24)

The existing BM baseline and historical comparison drivers included eight project
headers that existed only in an untracked working tree. This publication restores
those existing sources unchanged:

- `bm_page_core.hpp`, `bm_page_root.hpp`, `bm_page_index.hpp`, `bm_bounded_index.hpp`;
- `route_attribution_page.hpp`, `coupled_root.hpp`, `coupled_page_index.hpp`,
  `coupled_page_adapter.hpp`.

All are under `07_Eval_Integration/diagnostics/`. The second group is required by
the already-published native-model and short-directory comparison drivers.
This does not enable an experiment or change the Context16 default.

## Verify from a fresh clone

Requirements: Linux or WSL Ubuntu, Python 3.10+, Git, GCC/G++, and an x86-64-v3
CPU. Run from a clean checkout:

```bash
python3 -B tools/verify_bm_checkout.py --prepare-dependencies
```

The script fetches exactly the benchmark and model commits recorded in the
experiment, into ignored directories:

| Dependency | Commit |
|---|---|
| zsh20070718/learned-index | `003a55bf674b43350fbb70242070fb74f33a56ce` |
| Zephyr761/Learned-Index-Model | `3894c767338a865d2920ba5e21283827fea1d803` |

An existing dependency checkout must already be at the expected commit with no
tracked changes; the script never resets it. Once dependencies exist, omit
`--prepare-dependencies` for offline verification. The old fixed-path shell
scripts are historical experiment entry points; this new entry point discovers
the checkout location and does not patch project sources during a build.

Each invocation creates a new ignored `builds/bm_checkout_<timestamp>/` directory.
It runs both public factory contracts, checks the restored header closure, runs
the bounded candidate's dense-insert structural verification, and replays a
deterministic mixed-operation fixture through BM, BM split, ART, and Context16.
Both replay repetitions must match an independent Python reference checksum.

`report.json`, compiler logs, and `identity.json` are retained in that directory.
The identity records source hashes, pinned dependency hashes, compiler version,
and build commands; compile-time identity macros use actual hashes.

This check does not download corpus data or reproduce historical timings. It
validates source completeness, contracts, structural checks, and answer replay.
The corpus-specific performance, space, and adoption gates retain the boundaries
described in `07_Eval_Integration/CORRECTION_BM_TOTAL_20261024.md`.

## Recorded clean rebuild

Commit `bb55734442f1f66ef4d9b1786ac25b845f6e2658` was cloned with
`--no-local --no-hardlinks` into the WSL native filesystem and checked with
GCC 13.3.0. The dependencies were fetched at the exact commits above.

| Check | Result |
|---|---|
| Baseline public factory contract | 100/100 |
| Candidate public factory contract | 99/99 |
| Restored header closure | Compiled |
| Bounded dense-insert verification | 8,192 initial rows, 2,048 inserts, 41,826 checks, zero mismatches |
| BM / BM split / ART / Context16 replay | Both repetitions matched the independent reference checksum |

The machine-readable record and artifact hashes are in
[`results/bm_checkout_20261024/report.json`](../07_Eval_Integration/results/bm_checkout_20261024/report.json).
The hashes refer to the verifier's generated output files, including its original
unaugmented `report.json`.
The validation used only committed project files and freshly fetched upstream
dependencies. It did not use local corpus binaries, untracked project headers,
or previous build objects. This is a source-completeness check and does not
change any performance or adoption conclusion.

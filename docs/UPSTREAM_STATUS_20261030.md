# Upstream status checked 2026-10-30

Read-only GitHub connector checks of repository metadata and default branch refs:

| Role | Canonical repository | Current main SHA | Fixed experiment SHA |
|---|---|---|---|
| Benchmark | zsh20070718/learned-index | 003a55bf674b43350fbb70242070fb74f33a56ce | identical |
| Model | Zephyr761/Learned-Index-Model | 3894c767338a865d2920ba5e21283827fea1d803 | identical |

The user's original model URL `Zephyr761/Learned-Index` resolves to the canonical
`Zephyr761/Learned-Index-Model` repository. The default branch contents have not
advanced past the pins used by the current BM experiments. No dependency, snapshot,
remote or default configuration was changed by this check.

Sources: [benchmark main ref](https://api.github.com/repos/zsh20070718/learned-index/branches/main),
[model main ref](https://api.github.com/repos/Zephyr761/Learned-Index-Model/branches/main),
[benchmark commit](https://github.com/zsh20070718/learned-index/commit/003a55bf674b43350fbb70242070fb74f33a56ce),
[model commit](https://github.com/Zephyr761/Learned-Index-Model/commit/3894c767338a865d2920ba5e21283827fea1d803).

## Later check: suite tooling at 94ead07a

The model main remains 3894c767338a865d2920ba5e21283827fea1d803. Benchmark main is now 94ead07a45af7cb5dd33a2a708683ec2ba4411b6 (2026-10-30 09:14:48 UTC): one commit and 15 files after the frozen 003a55bf experiment version. The new doctor/demo/suite/require-all tooling and 15 suite tests are adopted as the next tool version; old experimental source pins and performance identities are retained. C++ contract, runner, and runner tests match the old experiment bytes. The repository evaluator gitlink previously was 403de526, separately from the 003a55bf measurement snapshot. No default model/index change.

77 source blob identities and 80 Python tool tests passed; native C++ and real-dataset checks still have failures, and standard Linux regression is blocked by WSL access. See results/upstream_benchmark_94ead07a_20261030 under 07_Eval_Integration and the structure performance journal; do not describe this as a complete performance acceptance.

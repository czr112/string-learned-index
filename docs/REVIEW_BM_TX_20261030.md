# Transaction split review

Fixed point `2f84688`, implementation `12933d8`; the follow-up working diff was
reviewed before formal measurements. The two review axes ran independently and
read-only using the code-review skill.

## Standards

One documented build-discipline finding: ART C sources were hashed after compilation
and were not scanned with gcc -MM. Fixed: scan the actual C dependency closure and
capture hashes before compilation, check again before object publication and at
run completion. The Standards reviewer confirmed the fix with no remaining blocker.
The isolated Index copy was accepted as intentional frozen-control separation.

## Spec

Three findings were resolved and rechecked:

- Direct historical-query candidate/BM1 comparison was missing. Added the explicit
  --control bm0|bm1 seam, preserving original query sets and all semantic checks.
- UBSan could report recoverable UB with exit status zero. Added
  -fno-sanitize-recover=all to sanitizer compilation before confirmation.
- Failure to meet a benefit target was named a regression. Added target_not_met,
  distinct from a confirmed >5% slowdown.

The Spec reviewer found no new defect in detached extraction, fence/model lifetimes,
allocation-free publication or resident growth/shrink accounting. A stale query-runner
docstring was subsequently corrected. No reviewer ran compilers or benchmarks.

Final empirical results and adoption limitations belong to the result report, not
this static review. Pointwise confidence intervals do not imply universal acceptance.

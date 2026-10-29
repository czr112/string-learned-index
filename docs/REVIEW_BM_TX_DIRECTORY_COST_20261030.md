# TX directory cost diagnosis review

Fixed point `fad6e43`, implementation `e52b6e2`, pre-measurement correction `d797999`.
Spec: PROTOCOL_BM_TX_DIRECTORY_COST_20261030.md. Both agents were read-only;
no compiler or measurement ran during review.

## Standards

Clear. Frozen preimages, namespace-copy generation, source identity, atomic binary
publication, per-cell input hashes, immutable outputs and semantic-before-timing
order conform to the documented discipline. No additional smell finding.

## Spec

Two pre-measurement points were fixed and rechecked:

- The branch-share estimand was ambiguous. Protocol now explicitly registers the
  per-process ratio of summed directory time to summed write time, followed by
  an arithmetic mean and t interval across four independent process blocks.
- Two extra timed warmup traces were discarded. They have been removed; only the
  registered observations run, and every measured trace contributes to a record.

No algorithm, oracle, allocation-accounting or preimage correctness blocker was found.
Reviewer confirmed both fixes before execution.

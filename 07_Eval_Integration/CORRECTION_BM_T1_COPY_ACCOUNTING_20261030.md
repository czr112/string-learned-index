# T1 directory-copy interpretation correction

The historical [T1 report](RESULTS_BM_T1_COST_20261028.md) measured net
`memory_bytes()` growth per block split (about 1.0–1.3 KB). That is a valid net
capacity measurement, but its subsequent use as a bound on directory-copy work
was not valid. It cannot establish that copying is negligible.

In the current [Page split path](diagnostics/bm_n1_page_core.hpp), `try_insert`
allocates replacement `next_blocks` and `next_fences`, reserves size+1, copies the
old ranges, inserts the two half blocks, and swaps only after preparation. For an
old directory of N blocks, the constructed replacement contains N+1 Block objects
and N+1 fences. The old directory is still alive during preparation. The net live
capacity increase after release is therefore different from the amount of existing
content reconstructed. The two quantities must not be substituted for one another.

This code inspection does not measure actual cache/bus traffic, duration or fraction
of write time. It also does not invalidate the earlier R-series measured ceiling
for eliminating full-page temporary row copies; those were a different component
and denominator. T2 duplicate-search and T3 arena-reuse failed results remain closed.

The next registered write diagnosis must measure current TX split frequency,
replacement-object/allocation work and elapsed component cost. Only a new material
cost bound can justify a bounded spare-capacity/local-directory candidate. No
performance benefit, accepted implementation or extra mechanism attempt is claimed
by this correction.

# T1 duplicate-probe interpretation correction

The old T1 microbenchmark compared `Index::find_base(existing)` to
`Index::insert(existing)`. In the latter, the Index-level `core->find` succeeds and
the function returns before Page::try_insert. Therefore this probe never executes
the second duplicate check on a successful insertion.

It also changes routing and lookup policy: public find_base uses the flat root and
the Page base window, while the insertion guard uses the map route and corrected
Page::find. Its observed 1.12–1.24 ratio remains a description of those two probes,
but cannot be interpreted as a 12%–24% duplicate-check component of insertion.
The historical report's corresponding cost-fraction inference is withdrawn.

The separate T2 full-trace intervention and its 16-block negative result are not
invalidated by this probe correction. Reopening it would need newly registered
evidence on the changed TX path. No new performance benefit or mechanism attempt
is asserted here.

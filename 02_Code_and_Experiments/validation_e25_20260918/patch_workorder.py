import io
p = '/mnt/d/StringLearnedIndex_Workspace/docs/WORKORDER_LOW_ACTIVITY_ENTRY_20261010.md'
s = io.open(p, encoding='utf-8').read()
add = u"""

---

## Outcome (2026-10-10, appended by the lead)

This work order was carried out; the round is closed with a **negative result** and the
candidate stays default-OFF. See `02_Code_and_Experiments/validation_e25_20260918/RESULTS_LOW_ACTIVITY_ENTRY.md`.

- The counters listed here were added to `index.hpp` (plus `hole_count_eq0/eq1` and
  `low_activity_helped`), wrapped in `#ifdef E25_DIAG` so the shipped path is unchanged.
- **The identity stated above is stale by the end of the round**: `index.hpp` went
  44702000 -> ec65540c0489bf9b798d3a3fb61fce054a8fc0723fbcee72c6c6889146e9ba8c (5,673 lines).
  `switches.hpp` is unchanged (e75c7fad..., 165 lines).
- Headline: the branch reaches only 183 / 27 / 88 passes over 200% replacement on
  mixed / skew / tranco, promoting 182 / 26 / 68 placements, against 977k / 1731k / 97k
  n<2 refusals. The big refusals sit on EMPTY blocks, which `first_candidate()` skips.
- Two defects in the diagnostic's own audit were fixed along the way (a random victim drawn
  from the corpus instead of the live pool, and an audit that counted re-replaced fresh keys
  as lost). Both are recorded in the results document, section 6.
"""
if u'## Outcome (2026-10-10' in s:
    print('already appended')
else:
    io.open(p, 'w', encoding='utf-8', newline='').write(s + add)
    print('appended outcome note')

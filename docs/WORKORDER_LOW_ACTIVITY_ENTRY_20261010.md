# Work order: low-activity sub-block reclaim, mechanism diagnostic (2026-10-10)

Scope: turn the entry gate into a mechanism diagnostic for the `reclaim_low_activity`
candidate in `02_Code_and_Experiments/validation_e25_20260918`. Do NOT change the default:
the switch stays `false` in `baseline_config` and `pin_frozen_switches`.

## Fixed context (do not re-derive)

- Source currently at `index.hpp` `44702000b68dc97edbf20acb9832de97a9377a70f2eda0071dc5eb19be69ce2a`,
  `switches.hpp` `e75c7fad79a1944af4b1ecc48761b8d8180568aa473d7f0a0f0872ef16d0880f`.
- Gate (`check_low_activity`, `san_check_low_activity`) passes 1032/1032 on mixed/skew/tranco.
  Branch hits: mixed 131072 -> 1 (of 46822 E31 repack passes); skew 131072 -> 0 (of 71308);
  tranco 262144 -> 0 (of 14). So the ENTRY exists but the opportunity is near zero there.
- Recorded gate output: `results/main/low_activity_gate_20261010.txt`.
- The candidate has two effects: `repack_subblock` acts on n<2, and `subblock_garbage`
  returns non-zero for n<2 (so `reclaim_widen` may rank/select those blocks). Both are in scope.

## What the plan requires (docs/NEXT_ACTION_PLAN_20261009.md, section "入口诊断")

"当前身份下重跑入口诊断：三语料、两后端，等长/变长替换，FIFO 与固定种子随机删除，
0/25/50/100/200% 检查点，纯插入作无垃圾对照。按同一失败目标块记录 n=0/n=1 的实际机会、
回收字节、促成放置及最终溢出；区分块级与插入级。"

This entry has only the RANDOM replacement (gate) and a single 100% depth so far. The
"long replacement to 200%" regime is the one where the earlier hole diagnosis found 53-98%
of block-level byte refusals landing on 0-1-record blocks (see RESULTS_HOLES_ENTRY.md).

## Deliverable 1 - counters (index.hpp)

Add to the E25 diagnostic counter struct, kept `E25_DIAG`-gated like their neighbours:

- `low_activity_n0`  - repack passes admitted by the candidate on a block with h.count == 0
- `low_activity_n1`  - the same with h.count == 1
- `low_activity_bytes` - bytes the admitted pass recovered on those blocks, computed as the
  block's garbage (`occupied - live`) BEFORE the pass runs. Add it inside the same branch that
  increments `low_activity_reclaimed`, before the packing loop mutates offsets. Do not guess:
  read `subblock_garbage` for the exact definition and keep one definition of "garbage".

No behaviour change: the counters must not alter any branch, and the existing
`low_activity_reclaimed` keeps meaning "passes admitted on n<2".

## Deliverable 2 - program (new file `diag_low_activity.cpp`)

Follow the conventions of `diag_holes.cpp` and `check_low_activity.cpp` (global namespace free
of `using namespace validation` leakage is not required; keep the include set consistent).
Build it through `tools_build.py` (add the target and its sanitizer twin; the file list and the
`-DE25_DIAG` mapping live there).

Usage: `diag_low_activity KIND N [TRANCOCSV]`

For (kind, mode, checkpoint) it runs TWO arms through the SAME operation sequence:
  arm off = E31 (`subblock_reclaim=true`), low_activity false, `reclaim_widest` false,
            `compact_region_routing` false
  arm on  = the same, low_activity true
and emits one machine-readable JSON line per (kind, mode, arm, checkpoint) with:
  counters of the segment (deltas, not totals): subblock_reclaims, subblock_reclaim_helped,
  low_activity_reclaimed, low_activity_n0, low_activity_n1, low_activity_bytes,
  reclaim_widened, reclaim_widened_helped, place_fail_slots, place_fail_bytes,
  compact_entered, compact_help<none>, compact_plan_rejected, overflow_inserts (E18),
  index size, live keys, and a memory figure that separates overflow from primary.
  Also emit a per-mode "total" row over 0-200%.
Modes:
  `fifo`   - erase the oldest live key (a std::deque as in diag_holes W1)
  `random` - erase a fixed-seed permutation of victims (as in diag_holes W3)
  `insert` - pure insert control (no erases) - report once, not per checkpoint
Checkpoints: 0.25, 0.50, 1.00, 2.00 of N.

Correctness inside the program (this is a gate as much as a diagnostic):
- The two arms must be verified equal on the observable after the full workload: size,
  every live key find()s to its value, a scan agrees row by row, and overflow_inserts is
  reported (not asserted equal).
- Assert `subblock_reclaims > 0` on every mode/corpus so a zero opportunity cannot come from
  the mechanism never running.
- Wrap the operation generator OUTSIDE the timed span; this program does not report timing.
- Fresh keys must be anchored on their victim as in check_low_activity so the key distribution
  does not drift (see the `~f`/`~r` construction there and in diag_holes).
- Fail loudly on live-count drift.

## Deliverable 3 - run and record

Write raw output to `results/main/low_activity_entry_<kind>_<mode>.jsonl` (new names; never
overwrite an existing file) and a text summary to `results/main/low_activity_entry_analysis.txt`.
Run the timing build for all 3 corpora and all 3 modes; tranco uses N=262144, mixed/skew use
131072. Then build and run the sanitizer twin on mixed 20000 and skew 20000 and record stderr.

## Deliverable 4 - report

Write `RESULTS_LOW_ACTIVITY_ENTRY.md` in the round directory, in the house style (claim tables
with numbers, explicit "not done" list). The headline question:
  does admitting n<2 blocks create a MEASURABLE net difference in overflow records,
  maintenance work or space at any checkpoint, and does the on-arm ever differ from the off-arm
  in a way that survives the counters?
If the on/off difference is zero or one event across all corpora and modes, say so and record
it as a negative result - do not dress it up. Distinguish "the branch never ran" (coverage gap)
from "the branch ran and changed nothing" (negative result).

## Building

Use `python3 tools_build.py <target>` then run `./build/<target>.exe` from WSL2 Ubuntu (the
Windows side cannot compile this code). Keep `logs/build_*.json` and `build/*.built.json`
consistent with `tools_build.py`'s existing behaviour; do not hand-edit them.

## Do not

- Do not change any default, any frozen switch, the model, the mapping, group geometry,
  the reclaim budget, the overflow container or the migration policy.
- Do not enable `reclaim_widest` as part of the candidate. It is a separate, already-answered
  question; this round is E31-only, so the single variable stays the low-activity guard.
- Do not delete or overwrite existing evidence.


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

# E22 tables: the frozen index under an update load

Source: `results/main/e22_dynamic.jsonl` (108 measured records = 3 processes x 3 datasets x 3 workloads x 4 checkpoints, plus 36 instrumented records).

| workload | what it does |
|---|---|
| **`update_only`** | **THE CONTROL.** Every key exists; only the value changes. Record count, capacity tables and the overflow count must NOT move. |
| `insert_only` | new keys that do not exist, anchored on real keys so they spread over the key space rather than piling up at one end |
| `replace` | one erase + one insert per cycle, holding the live record count constant |

Each workload starts from its OWN fresh initial state. The timed update span contains
only index operations -- query verification, key generation and accounting are all
outside it. A replace cycle is reported as one cycle AND two operations.

**This round observes only. No maintenance policy is changed.**

## 1. The control

`update_only` at every checkpoint against its own start:

| dataset | records | overflow | capacity tables changed | compactions | rebuilds | splits | space |
|---|---|---|---|---|---|---|---|
| mixed | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** |
| skew | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** |
| tranco | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** | **unchanged** |

**The control holds exactly on every dataset: nothing structural moves under
value-only updates.** That is what makes the rest of the round readable -- the
changes reported below can be attributed to the operations, not to the measurement.

## 2. Structural metrics

### `mixed` (131,072 initial keys)

| workload | checkpoint | records | overflow | overflow share | compactions | rebuilds | splits | **fallback regions** | fallback record share | fallback query share (set A) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `update_only` | 0% | 131,072 | 751 | 0.6% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 5% | 131,072 | 751 | 0.6% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 10% | 131,072 | 751 | 0.6% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 25% | 131,072 | 751 | 0.6% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 0% | 131,072 | 751 | 0.6% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 5% | 137,625 | 7,290 | 5.3% | 59 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 10% | 144,179 | 13,839 | 9.6% | 186 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 25% | 163,840 | 33,486 | 20.4% | 531 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 0% | 131,072 | 751 | 0.6% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 5% | 131,072 | 1,671 | 1.3% | 86 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 10% | 131,072 | 3,030 | 2.3% | 203 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 25% | 131,072 | 12,877 | 9.8% | 1,131 | 0 | 0 | **0** | 0.0% | 0.0% |

### `skew` (131,072 initial keys)

| workload | checkpoint | records | overflow | overflow share | compactions | rebuilds | splits | **fallback regions** | fallback record share | fallback query share (set A) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `update_only` | 0% | 131,072 | 4,901 | 3.7% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 5% | 131,072 | 4,901 | 3.7% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 10% | 131,072 | 4,901 | 3.7% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 25% | 131,072 | 4,901 | 3.7% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 0% | 131,072 | 4,901 | 3.7% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 5% | 137,625 | 11,394 | 8.3% | 1,356 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 10% | 144,179 | 17,924 | 12.4% | 1,960 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 25% | 163,840 | 37,578 | 22.9% | 2,168 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 0% | 131,072 | 4,901 | 3.7% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 5% | 131,072 | 11,179 | 8.5% | 2,052 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 10% | 131,072 | 17,483 | 13.3% | 3,106 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 25% | 131,072 | 36,178 | 27.6% | 5,103 | 0 | 0 | **0** | 0.0% | 0.0% |

### `tranco` (262,144 initial keys)

| workload | checkpoint | records | overflow | overflow share | compactions | rebuilds | splits | **fallback regions** | fallback record share | fallback query share (set A) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `update_only` | 0% | 262,144 | 11,759 | 4.5% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 5% | 262,144 | 11,759 | 4.5% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 10% | 262,144 | 11,759 | 4.5% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `update_only` | 25% | 262,144 | 11,759 | 4.5% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 0% | 262,144 | 11,759 | 4.5% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 5% | 275,251 | 24,757 | 9.0% | 660 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 10% | 288,358 | 37,747 | 13.1% | 1,360 | 0 | 0 | **0** | 0.0% | 0.0% |
| `insert_only` | 25% | 327,680 | 76,963 | 23.5% | 1,901 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 0% | 262,144 | 11,759 | 4.5% | 0 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 5% | 262,144 | 17,154 | 6.5% | 707 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 10% | 262,144 | 23,128 | 8.8% | 1,460 | 0 | 0 | **0** | 0.0% | 0.0% |
| `replace` | 25% | 262,144 | 39,817 | 15.2% | 4,065 | 0 | 0 | **0** | 0.0% | 0.0% |

**Capacity fallback never happens.** A rebuild is what would replace a region with the
uniform default, and no rebuild occurs under any of these workloads: the inserts are
anchored on real keys, so no region's prefix changes and no split is forced. **The
demand layout is preserved; the channel through which maintenance shows up is OVERFLOW
GROWTH**, not capacity reverting.

That also means the two coverage metrics come out at zero, and they are reported anyway:
had the layout reverted, they are what would have said how much data and how much traffic
was affected.

## 3. Space over time (MB)

### `mixed`

| workload | checkpoint | prototype | ART | prototype / ART |
|---|---|---:|---:|---:|
| `update_only` | 0% | 23.95 | 22.26 | **1.08x** |
| `update_only` | 5% | 23.95 | 22.26 | **1.08x** |
| `update_only` | 10% | 23.95 | 22.26 | **1.08x** |
| `update_only` | 25% | 23.95 | 22.26 | **1.08x** |
| `insert_only` | 0% | 23.95 | 22.26 | **1.08x** |
| `insert_only` | 5% | 24.71 | 23.14 | **1.07x** |
| `insert_only` | 10% | 26.05 | 24.71 | **1.05x** |
| `insert_only` | 25% | 29.14 | 28.73 | **1.01x** |
| `replace` | 0% | 23.95 | 22.26 | **1.08x** |
| `replace` | 5% | 24.06 | 22.37 | **1.08x** |
| `replace` | 10% | 24.21 | 22.47 | **1.08x** |
| `replace` | 25% | 26.00 | 22.50 | **1.16x** |

### `skew`

| workload | checkpoint | prototype | ART | prototype / ART |
|---|---|---:|---:|---:|
| `update_only` | 0% | 23.71 | 27.70 | **0.86x** |
| `update_only` | 5% | 23.71 | 27.70 | **0.86x** |
| `update_only` | 10% | 23.71 | 27.70 | **0.86x** |
| `update_only` | 25% | 23.71 | 27.70 | **0.86x** |
| `insert_only` | 0% | 23.71 | 27.70 | **0.86x** |
| `insert_only` | 5% | 24.53 | 29.27 | **0.84x** |
| `insert_only` | 10% | 25.59 | 30.84 | **0.83x** |
| `insert_only` | 25% | 28.75 | 35.56 | **0.81x** |
| `replace` | 0% | 23.71 | 27.70 | **0.86x** |
| `replace` | 5% | 24.45 | 27.70 | **0.88x** |
| `replace` | 10% | 25.28 | 27.70 | **0.91x** |
| `replace` | 25% | 28.29 | 27.70 | **1.02x** |

### `tranco`

| workload | checkpoint | prototype | ART | prototype / ART |
|---|---|---:|---:|---:|
| `update_only` | 0% | 26.35 | 19.14 | **1.38x** |
| `update_only` | 5% | 26.35 | 19.14 | **1.38x** |
| `update_only` | 10% | 26.35 | 19.14 | **1.38x** |
| `update_only` | 25% | 26.35 | 19.14 | **1.38x** |
| `insert_only` | 0% | 26.35 | 19.14 | **1.38x** |
| `insert_only` | 5% | 27.56 | 20.45 | **1.35x** |
| `insert_only` | 10% | 28.77 | 21.77 | **1.32x** |
| `insert_only` | 25% | 32.33 | 25.72 | **1.26x** |
| `replace` | 0% | 26.35 | 19.14 | **1.38x** |
| `replace` | 5% | 26.85 | 19.20 | **1.40x** |
| `replace` | 10% | 27.42 | 19.28 | **1.42x** |
| `replace` | 25% | 28.99 | 19.49 | **1.49x** |

### How long does the `skew` advantage last?

E21 measured the prototype **14% smaller** than ART on `skew` at rest. Over the load:

| workload | 0% | 5% | 10% | 25% |
|---|---:|---:|---:|---:|
| `update_only` | 0.86x | 0.86x | 0.86x | 0.86x |
| `insert_only` | 0.86x | 0.84x | 0.83x | 0.81x |
| `replace` | 0.86x | 0.88x | 0.91x | 1.02x |

**Value-only updates: it holds exactly** (nothing moves on either side).
**Inserts: it survives, and widens** -- both grow, the prototype more slowly.
**Replacement: it is gone by the 25% checkpoint** -- the prototype grows while ART stays
flat, because inserted keys land in overflow while erased ones leave their bytes behind.

## 4. Query cost at the checkpoints (ns/query)

Two query sets, reported separately:

* **A: a FIXED key sequence.** The same access demand at every checkpoint; the true
  answer is recomputed from the live state, so the hit rate falls as keys are replaced.
  This is the one that shows whether the SAME demand gets slower.
* **B: regenerated to a fixed 50% hit ratio** from the current live keys and keys verified
  absent. This keeps the hit rate from masking a layout change.

**Noise floor**: the 0% checkpoint is the same untouched state, measured once per workload,
so its spread across those measurements is what a difference has to beat.

At rest, the same state measured 9 times (3 datasets x 3 workloads x 2 sets x 2 methods)
spreads by a median of **8%** and up to **22%**.

### `mixed`

| workload | checkpoint | A: proto | A: ART | A: ratio | A hit | B: proto | B: ART | B: ratio |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `update_only` | 0% | 328 | 246 | 1.34x | 100.0% | 378 | 301 | 1.26x |
| `update_only` | 5% | 341 | 249 | 1.37x | 100.0% | 384 | 295 | 1.30x |
| `update_only` | 10% | 307 | 243 | 1.27x | 100.0% | 364 | 301 | 1.21x |
| `update_only` | 25% | 351 | 268 | 1.31x | 100.0% | 378 | 305 | 1.24x |
| `insert_only` | 0% | 337 | 270 | 1.25x | 100.0% | 361 | 297 | 1.22x |
| `insert_only` | 5% | 297 | 233 | 1.27x | 100.0% | 422 | 311 | 1.36x |
| `insert_only` | 10% | 354 | 288 | 1.23x | 100.0% | 423 | 390 | 1.08x |
| `insert_only` | 25% | 344 | 293 | 1.18x | 100.0% | 488 | 333 | 1.47x |
| `replace` | 0% | 333 | 296 | 1.13x | 100.0% | 380 | 306 | 1.24x |
| `replace` | 5% | 331 | 262 | 1.26x | 94.9% | 379 | 314 | 1.21x |
| `replace` | 10% | 318 | 306 | 1.04x | 90.0% | 367 | 305 | 1.20x |
| `replace` | 25% | 362 | 257 | 1.41x | 74.9% | 451 | 339 | 1.33x |

### `skew`

| workload | checkpoint | A: proto | A: ART | A: ratio | A hit | B: proto | B: ART | B: ratio |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `update_only` | 0% | 409 | 432 | 0.95x | 100.0% | 517 | 511 | 1.01x |
| `update_only` | 5% | 393 | 453 | 0.87x | 100.0% | 503 | 491 | 1.02x |
| `update_only` | 10% | 398 | 477 | 0.84x | 100.0% | 491 | 448 | 1.10x |
| `update_only` | 25% | 381 | 484 | 0.79x | 100.0% | 504 | 427 | 1.18x |
| `insert_only` | 0% | 409 | 532 | 0.77x | 100.0% | 505 | 437 | 1.16x |
| `insert_only` | 5% | 454 | 451 | 1.01x | 100.0% | 499 | 447 | 1.12x |
| `insert_only` | 10% | 430 | 559 | 0.77x | 100.0% | 568 | 471 | 1.21x |
| `insert_only` | 25% | 433 | 512 | 0.85x | 100.0% | 642 | 531 | 1.21x |
| `replace` | 0% | 399 | 453 | 0.88x | 100.0% | 490 | 493 | 0.99x |
| `replace` | 5% | 424 | 463 | 0.91x | 94.9% | 526 | 499 | 1.05x |
| `replace` | 10% | 438 | 471 | 0.93x | 90.0% | 568 | 510 | 1.11x |
| `replace` | 25% | 488 | 445 | 1.10x | 74.9% | 599 | 495 | 1.21x |

### `tranco`

| workload | checkpoint | A: proto | A: ART | A: ratio | A hit | B: proto | B: ART | B: ratio |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `update_only` | 0% | 312 | 192 | 1.63x | 100.0% | 340 | 207 | 1.64x |
| `update_only` | 5% | 340 | 193 | 1.76x | 100.0% | 357 | 220 | 1.62x |
| `update_only` | 10% | 343 | 211 | 1.63x | 100.0% | 375 | 213 | 1.76x |
| `update_only` | 25% | 321 | 205 | 1.57x | 100.0% | 340 | 205 | 1.66x |
| `insert_only` | 0% | 348 | 191 | 1.82x | 100.0% | 355 | 220 | 1.61x |
| `insert_only` | 5% | 318 | 207 | 1.54x | 100.0% | 356 | 225 | 1.58x |
| `insert_only` | 10% | 342 | 212 | 1.61x | 100.0% | 381 | 221 | 1.73x |
| `insert_only` | 25% | 321 | 253 | 1.27x | 100.0% | 411 | 257 | 1.60x |
| `replace` | 0% | 345 | 211 | 1.64x | 100.0% | 360 | 247 | 1.46x |
| `replace` | 5% | 324 | 203 | 1.59x | 94.9% | 347 | 220 | 1.58x |
| `replace` | 10% | 367 | 195 | 1.88x | 89.8% | 344 | 232 | 1.48x |
| `replace` | 25% | 351 | 195 | 1.79x | 74.7% | 383 | 195 | 1.97x |

## 5. Update cost (ms per segment, median across processes)

The timed span contains only index operations. A replace cycle is two operations.

### `mixed`

| workload | checkpoint | cycles | ops | prototype (ms) | ART (ms) |
|---|---|---:|---:|---:|---:|
| `update_only` | 5% | 0 | 6,553 | 5.4 | 3.9 |
| `update_only` | 10% | 0 | 13,107 | 5.6 | 3.4 |
| `update_only` | 25% | 0 | 32,768 | 14.9 | 9.7 |
| `insert_only` | 5% | 0 | 6,553 | 3.1 | 1.5 |
| `insert_only` | 10% | 0 | 13,107 | 6.9 | 2.9 |
| `insert_only` | 25% | 0 | 32,768 | 30.6 | 9.7 |
| `replace` | 5% | 6,553 | 13,106 | 4.4 | 1.5 |
| `replace` | 10% | 13,107 | 26,214 | 3.8 | 1.4 |
| `replace` | 25% | 32,768 | 65,536 | 55.0 | 8.9 |

### `skew`

| workload | checkpoint | cycles | ops | prototype (ms) | ART (ms) |
|---|---|---:|---:|---:|---:|
| `update_only` | 5% | 0 | 6,553 | 7.7 | 5.4 |
| `update_only` | 10% | 0 | 13,107 | 10.0 | 5.7 |
| `update_only` | 25% | 0 | 32,768 | 22.9 | 18.6 |
| `insert_only` | 5% | 0 | 6,553 | 84.0 | 7.5 |
| `insert_only` | 10% | 0 | 13,107 | 33.1 | 6.0 |
| `insert_only` | 25% | 0 | 32,768 | 28.6 | 17.3 |
| `replace` | 5% | 6,553 | 13,106 | 109.8 | 7.7 |
| `replace` | 10% | 13,107 | 26,214 | 53.5 | 6.8 |
| `replace` | 25% | 32,768 | 65,536 | 96.7 | 16.9 |

### `tranco`

| workload | checkpoint | cycles | ops | prototype (ms) | ART (ms) |
|---|---|---:|---:|---:|---:|
| `update_only` | 5% | 0 | 13,107 | 9.1 | 4.2 |
| `update_only` | 10% | 0 | 26,214 | 9.8 | 4.2 |
| `update_only` | 25% | 0 | 65,536 | 30.4 | 11.0 |
| `insert_only` | 5% | 0 | 13,107 | 20.4 | 3.7 |
| `insert_only` | 10% | 0 | 26,214 | 22.0 | 4.2 |
| `insert_only` | 25% | 0 | 65,536 | 38.0 | 11.4 |
| `replace` | 5% | 13,107 | 26,214 | 25.0 | 3.5 |
| `replace` | 10% | 26,214 | 52,428 | 25.2 | 3.5 |
| `replace` | 25% | 65,536 | 131,072 | 87.9 | 10.3 |


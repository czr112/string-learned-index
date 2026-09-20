# E21 tables: point queries on the frozen default

Source: `results/main/e21_timings.csv` (3240 timed rows = 5 processes x 3 seeds x 5 rounds x 3 query classes x 4 methods) and `results/main/e21_build.jsonl`.

| method | what it is |
|---|---|
| `sorted_array` | `std::lower_bound` over the shared sorted keys, which it does NOT copy |
| `ART` | native armong/libart |
| `S_original` | the two-pass construction (`bulk_load` + `rebuild_with_caps`) |
| **`S_frozen`** | **the frozen default**: statistics -> plan -> direct build + R1 |

Every method is verified query by query outside the timed region, then rounds are shuffled,
warmed and checksummed. Timings are ns per query, median over every batch. Data, query
sequence, leaf config, routing and overflow policy are identical across methods.

## 1. Gate: the two S methods are the same structure

`S_frozen` optimises CONSTRUCTION. It is not expected to query faster than `S_original`, and
it should not be: the two build the same structure, which is asserted by digest before any
query is timed. Any timing difference between them is therefore noise, and the round's job
is to CONFIRM that rather than assume it.

| dataset | S digest, frozen vs original |
|---|---|
| mixed | **identical** |
| skew | **identical** |
| tranco | **identical** |

## 2. Point-query cost (ns/query)

### `mixed`

| class | ART | S_frozen | S_original | sorted_array |
|---|---:|---:|---:|---:|
| all | 321.4 | 404.7 | 403.2 | 729.1 |
| hit | 259.5 | 341.0 | 333.4 | 636.1 |
| miss | 235.8 | 255.5 | 253.0 | 621.2 |

| ratio | all | hit | miss |
|---|---:|---:|---:|
| **S_frozen / ART** | 1.259x | 1.314x | 1.083x |
| **S_frozen / sorted_array** | 0.555x | 0.536x | 0.411x |
| S_frozen / S_original | 1.004x | 1.023x | 1.010x |

### `skew`

| class | ART | S_frozen | S_original | sorted_array |
|---|---:|---:|---:|---:|
| all | 489.0 | 550.3 | 549.1 | 837.5 |
| hit | 442.6 | 432.3 | 437.8 | 770.4 |
| miss | 403.4 | 387.0 | 381.7 | 755.0 |

| ratio | all | hit | miss |
|---|---:|---:|---:|
| **S_frozen / ART** | 1.125x | 0.977x | 0.959x |
| **S_frozen / sorted_array** | 0.657x | 0.561x | 0.513x |
| S_frozen / S_original | 1.002x | 0.987x | 1.014x |

### `tranco`

| class | ART | S_frozen | S_original | sorted_array |
|---|---:|---:|---:|---:|
| all | 231.8 | 345.9 | 345.2 | 543.9 |
| hit | 205.6 | 326.4 | 334.4 | 514.4 |
| miss | 196.3 | 270.7 | 268.8 | 449.9 |

| ratio | all | hit | miss |
|---|---:|---:|---:|
| **S_frozen / ART** | 1.492x | 1.588x | 1.379x |
| **S_frozen / sorted_array** | 0.636x | 0.635x | 0.602x |
| S_frozen / S_original | 1.002x | 0.976x | 1.007x |

### The three classes are three LOADS, not a decomposition

`hits` and `misses` are separate query vectors, each timed as its own pass -- they are not
the mixed run split into two halves. So the three columns answer "how does S compare with
ART on this KIND of load", not "where does the mixed time go". E13 documented the same
property for its own three classes.

The check below makes that concrete: `(hit + miss) / 2` is what the mixed load would cost
if the two halves simply averaged, and the measured mixed load is well above it.

| dataset | method | all | hit | miss | (hit+miss)/2 | all / that |
|---|---|---:|---:|---:|---:|---:|
| mixed | ART | 321.4 | 259.5 | 235.8 | 247.6 | **1.298x** |
| mixed | S_frozen | 404.7 | 341.0 | 255.5 | 298.2 | **1.357x** |
| skew | ART | 489.0 | 442.6 | 403.4 | 423.0 | **1.156x** |
| skew | S_frozen | 550.3 | 432.3 | 387.0 | 409.7 | **1.343x** |
| tranco | ART | 231.8 | 205.6 | 196.3 | 200.9 | **1.154x** |
| tranco | S_frozen | 345.9 | 326.4 | 270.7 | 298.6 | **1.159x** |

The weighting above is exact, not approximate: `make_queries` sets `hit = (i % 2 == 0)`,
so every batch is exactly 50% hits (the run logs confirm `hits=65536 misses=65536`).

**The mixing penalty is larger for S than for ART**, and that is what decides the mixed
column. **It is a descriptive quantity, not an explanation** -- cache state, branch
behaviour, working-set size and execution order can all contribute, and this round does
not separate them, so no single mechanism is claimed:

| dataset | S penalty (all / implied) | ART penalty | S - ART |
|---|---:|---:|---:|
| mixed | 1.357x | 1.298x | +0.059 |
| skew | 1.343x | 1.156x | +0.187 |
| tranco | 1.159x | 1.154x | +0.005 |

**`skew` is where this is decisive.** S is faster on BOTH homogeneous loads --
hit 0.977x, miss 0.959x -- yet slower on the mixed load
because its mixing penalty is far larger. **So the three columns must not be read as
"where S loses": they are three loads, and the mixed column is not implied by the other
two.** A summary like "the gap is concentrated in hits" does not follow, and is withdrawn.

What can be said is only that **mixing hits and misses costs S proportionally more than it
costs ART**, most sharply on `skew`. Why is not established here.

## 3. Cross-process stability

Ordering of the four methods, per dataset and class. The two S methods are the same
structure and are expected to interleave.

| dataset | class | fastest -> slowest |
|---|---|---|
| mixed | all | ART < S_original < S_frozen < sorted_array |
| mixed | hit | ART < S_original < S_frozen < sorted_array |
| mixed | miss | ART < S_original < S_frozen < sorted_array |
| skew | all | ART < S_original < S_frozen < sorted_array |
| skew | hit | S_frozen < S_original < ART < sorted_array |
| skew | miss | S_original < S_frozen < ART < sorted_array |
| tranco | all | ART < S_original < S_frozen < sorted_array |
| tranco | hit | ART < S_frozen < S_original < sorted_array |
| tranco | miss | ART < S_original < S_frozen < sorted_array |

## 4. Build time and space

**Build time is where the frozen version differs from `S_original`** -- that is what R1 and
the construction changes were for. Query cost is not.

| method | mixed | skew | tranco |
|---|---:|---:|---:|
| ART build (ms) | 46.66 | 73.31 | 41.19 |
| S_frozen build (ms) | 71.16 | 79.97 | 110.36 |
| S_original build (ms) | 134.44 | 138.24 | 205.56 |
| sorted_array build (ms) | 0.00 | 0.00 | 0.00 |

| method | mixed | skew | tranco |
|---|---:|---:|---:|
| ART space (MB) | 22.26 | 27.70 | 19.15 |
| S_frozen space (MB) | 23.95 | 23.71 | 26.35 |
| S_original space (MB) | 23.95 | 23.71 | 26.35 |
| sorted_array space (MB) | 20.88 | 34.23 | 14.91 |

Space accounting differs by method and the numbers are not directly comparable across
rows: `S_*` use the prototype's own `memory()` (allocator metadata excluded, not RSS),
`ART` is measured by wrapping its allocator, and `sorted_array` counts the shared key
vectors it does not copy. The per-row caveat matters more than the comparison.


# E23 tables: range scan on the frozen default

Source: `results/main/e23_scan.jsonl`, 81 measured records (3 processes x 3 datasets x 3 states x 3 lengths).

**Semantics, shared by every ranked method**: from the first record with `key >= start`,
return AT MOST `n` records in key order, as complete key/value pairs. Both ranked methods
copy the keys into a `std::vector<Pair>`; a method returning views would be measuring
something else.

**ART is not in the ranking.** libart exposes `art_iter` (whole-tree traversal) and
`art_iter_prefix`, and nothing that seeks to an arbitrary key. A full-traversal adapter
measures the adapter, not ART's range capability, so it appears only in section 4 as a
clearly-labelled diagnostic. Its cost dominates this round's wall time -- itself a sign
of why it is not comparable.

States: the initial one, after inserts to 25%, and after replacements to 25%. Each is
built from its own fresh initial state. The value-only workload is not repeated: E22
established that it changes nothing structural.

## 1. Correctness

Every scan is compared against a reference map, **outside the timed span**, by full
equality of the returned vector -- not by a checksum. The timed pass keeps a checksum and
fails the run if it does not match. A mismatch throws, so a record existing at all means
all
4,096 scans per row verified.

That covers cross-region boundaries, the primary/overflow merge, and starts that fall
between two keys (a fifth of the starts are synthetic keys that exist in no bucket).

**The sub-block shape that exposed E9's defect** -- an EMPTY INTERMEDIATE sub-block, which
caused a scan to silently drop every later sub-block of that group:

| dataset | state | multiblock groups | empty intermediate sub-blocks | max sub-blocks |
|---|---|---:|---:|---:|
| mixed | `initial` | 223 | **0** | 30 |
| mixed | `insert25` | 223 | **0** | 30 |
| mixed | `replace25` | 223 | **6** | 30 |
| skew | `initial` | 582 | **0** | 22 |
| skew | `insert25` | 582 | **0** | 22 |
| skew | `replace25` | 582 | **55** | 22 |
| tranco | `initial` | 1,413 | **0** | 15 |
| tranco | `insert25` | 1,413 | **0** | 15 |
| tranco | `replace25` | 1,413 | **0** | 15 |

The replacement state does produce empty intermediate sub-blocks, so the scan path that
E9 broke is exercised here -- and every scan on it verified. **This is not a re-test of
the E9 defect**: the fix landed in E11, and E11's own gate covers it. It is the statement
that the scan being measured now is being measured on that shape.

## 2. Scan cost (ns per scan)

### `mixed`

| state | n | prototype | sorted_array | prototype / sorted_array |
|---|---:|---:|---:|---:|
| `initial` | 16 | 1,847 | 1,442 | **1.28x** |
| `initial` | 128 | 10,623 | 5,807 | **1.83x** |
| `initial` | 1024 | 67,187 | 40,022 | **1.68x** |
| `insert25` | 16 | 2,180 | 1,192 | **1.83x** |
| `insert25` | 128 | 11,626 | 6,513 | **1.78x** |
| `insert25` | 1024 | 73,506 | 41,796 | **1.76x** |
| `replace25` | 16 | 2,143 | 1,319 | **1.62x** |
| `replace25` | 128 | 12,383 | 6,500 | **1.91x** |
| `replace25` | 1024 | 76,814 | 44,658 | **1.72x** |

### `skew`

| state | n | prototype | sorted_array | prototype / sorted_array |
|---|---:|---:|---:|---:|
| `initial` | 16 | 2,244 | 1,503 | **1.49x** |
| `initial` | 128 | 9,893 | 5,888 | **1.68x** |
| `initial` | 1024 | 65,550 | 43,449 | **1.51x** |
| `insert25` | 16 | 2,761 | 1,638 | **1.69x** |
| `insert25` | 128 | 11,073 | 5,927 | **1.87x** |
| `insert25` | 1024 | 68,824 | 44,169 | **1.56x** |
| `replace25` | 16 | 2,835 | 1,430 | **1.98x** |
| `replace25` | 128 | 10,921 | 5,934 | **1.84x** |
| `replace25` | 1024 | 70,118 | 44,627 | **1.57x** |

### `tranco`

| state | n | prototype | sorted_array | prototype / sorted_array |
|---|---:|---:|---:|---:|
| `initial` | 16 | 1,668 | 991 | **1.68x** |
| `initial` | 128 | 7,941 | 4,048 | **1.96x** |
| `initial` | 1024 | 54,653 | 24,729 | **2.21x** |
| `insert25` | 16 | 1,919 | 1,230 | **1.56x** |
| `insert25` | 128 | 10,401 | 5,202 | **2.00x** |
| `insert25` | 1024 | 65,649 | 29,939 | **2.19x** |
| `replace25` | 16 | 1,722 | 1,007 | **1.71x** |
| `replace25` | 128 | 10,043 | 5,576 | **1.80x** |
| `replace25` | 1024 | 69,733 | 29,785 | **2.34x** |

**The point-query advantage does NOT carry over to scans: the prototype is slower than the
plain sorted array on every state and every length.** That is the opposite of the E21
result, where it was 1.5x-2.4x faster than the same baseline on point queries.

## 3. How many scans were short

A fifth of the starts are near the end of the key space, so the range holds fewer than `n`
records. Those runs are counted and reported, so a short range is never silently read as a
full scan.

| dataset | state | n | scans | short | minimum returned |
|---|---|---:|---:|---:|---:|
| mixed | `initial` | 16 | 4,096 | 6 | 3 |
| mixed | `initial` | 128 | 4,096 | 50 | 3 |
| mixed | `initial` | 1024 | 4,096 | 436 | 3 |
| mixed | `insert25` | 16 | 4,096 | 6 | 3 |
| mixed | `insert25` | 128 | 4,096 | 48 | 3 |
| mixed | `insert25` | 1024 | 4,096 | 435 | 3 |
| mixed | `replace25` | 16 | 4,096 | 6 | 3 |
| mixed | `replace25` | 128 | 4,096 | 50 | 3 |
| mixed | `replace25` | 1024 | 4,096 | 436 | 3 |
| skew | `initial` | 16 | 4,096 | 6 | 3 |
| skew | `initial` | 128 | 4,096 | 50 | 3 |
| skew | `initial` | 1024 | 4,096 | 436 | 3 |
| skew | `insert25` | 16 | 4,096 | 6 | 3 |
| skew | `insert25` | 128 | 4,096 | 48 | 3 |
| skew | `insert25` | 1024 | 4,096 | 435 | 3 |
| skew | `replace25` | 16 | 4,096 | 6 | 3 |
| skew | `replace25` | 128 | 4,096 | 50 | 3 |
| skew | `replace25` | 1024 | 4,096 | 436 | 3 |
| tranco | `initial` | 16 | 4,096 | 6 | 3 |
| tranco | `initial` | 128 | 4,096 | 49 | 3 |
| tranco | `initial` | 1024 | 4,096 | 425 | 3 |
| tranco | `insert25` | 16 | 4,096 | 6 | 3 |
| tranco | `insert25` | 128 | 4,096 | 48 | 3 |
| tranco | `insert25` | 1024 | 4,096 | 427 | 3 |
| tranco | `replace25` | 16 | 4,096 | 6 | 3 |
| tranco | `replace25` | 128 | 4,096 | 49 | 3 |
| tranco | `replace25` | 1024 | 4,096 | 425 | 3 |

## 4. The ART full-traversal diagnostic -- NOT RANKED

**This is not a measurement of ART's range capability.** libart has no seek-to-key
iterator, so this adapter walks the entire tree from the beginning and stops once it has
collected `n` records at or after `start`. Everything before the start is visited and
discarded.

| dataset | n | diagnostic (ns) | prototype (ns) | diagnostic / prototype |
|---|---:|---:|---:|---:|
| mixed | 16 | 2,815,944 | 2,143 | **1,314x** |
| mixed | 128 | 3,023,895 | 11,626 | **260x** |
| mixed | 1024 | 2,944,424 | 73,506 | **40x** |
| skew | 16 | 3,412,723 | 2,761 | **1,236x** |
| skew | 128 | 3,387,513 | 10,921 | **310x** |
| skew | 1024 | 3,417,237 | 68,824 | **50x** |
| tranco | 16 | 6,583,549 | 1,722 | **3,823x** |
| tranco | 128 | 6,724,708 | 10,043 | **670x** |
| tranco | 1024 | 6,885,802 | 65,649 | **105x** |

It is two to three ORDERS of magnitude slower, and its cost scales with the number of keys
rather than with `n`. **A mature ART range-scan implementation would not look like this**,
so the number is reported to show what the adapter costs and nothing more.


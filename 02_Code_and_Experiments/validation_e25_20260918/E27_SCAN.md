# E27: what a range scan spends, and what one change buys

Sources: `results/main/e27_scan_diag.jsonl` (27 records, counts) and `results/main/e27_scan_time.jsonl` (2025 records, clean timing).

Scan semantics are E23's, unchanged: **from the first record with `key >= start`,
return at most `n` records in key order as complete key/value pairs.** States,
lengths and the start list (four fifths real keys, one fifth synthetic keys that
exist in no bucket) are E23's, read from the protocol rather than remembered.

Every scan's output is checked against the reference **outside** the counted
window; a mismatch aborts the run.

## 1. The four quantities, measured before choosing

Per scan, over the protocol's start list.

| dataset | state | n | start locates | regions | groups | **empty groups** | sub-blocks skipped | merge compares | **result allocations** |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| mixed | `initial` | 16 | 2.01 | 1.01 | 3.7 | **0.0** | 2.9 | 3.7 | **31.96** |
| mixed | `initial` | 128 | 2.06 | 1.06 | 22.2 | **0.0** | 22.3 | 29.6 | **255.58** |
| mixed | `initial` | 1024 | 2.49 | 1.49 | 168.1 | **0.0** | 175.3 | 236.7 | **2037.69** |
| mixed | `insert25` | 16 | 2.01 | 1.01 | 3.3 | **0.0** | 2.4 | 14.9 | **28.85** |
| mixed | `insert25` | 128 | 2.05 | 1.05 | 18.1 | **0.0** | 18.0 | 119.4 | **230.51** |
| mixed | `insert25` | 1024 | 2.39 | 1.39 | 134.7 | **0.0** | 140.5 | 951.7 | **1837.76** |
| mixed | `replace25` | 16 | 2.01 | 1.01 | 3.7 | **0.0** | 2.9 | 10.3 | **30.46** |
| mixed | `replace25` | 128 | 2.06 | 1.06 | 22.2 | **0.0** | 22.3 | 82.3 | **243.73** |
| mixed | `replace25` | 1024 | 2.49 | 1.49 | 168.1 | **0.0** | 175.3 | 658.1 | **1942.99** |
| skew | `initial` | 16 | 2.01 | 1.01 | 3.1 | **0.0** | 2.4 | 8.3 | **31.91** |
| skew | `initial` | 128 | 2.06 | 1.06 | 20.3 | **0.0** | 21.8 | 65.0 | **255.04** |
| skew | `initial` | 1024 | 2.49 | 1.49 | 160.3 | **0.0** | 179.6 | 519.8 | **2033.60** |
| skew | `insert25` | 16 | 2.01 | 1.01 | 2.6 | **0.0** | 1.9 | 15.8 | **28.82** |
| skew | `insert25` | 128 | 2.05 | 1.05 | 16.2 | **0.0** | 17.2 | 126.0 | **230.13** |
| skew | `insert25` | 1024 | 2.39 | 1.39 | 130.0 | **0.0** | 145.4 | 1004.6 | **1834.96** |
| skew | `replace25` | 16 | 2.01 | 1.01 | 3.1 | **0.0** | 2.4 | 13.9 | **28.03** |
| skew | `replace25` | 128 | 2.06 | 1.06 | 20.3 | **0.0** | 21.8 | 110.8 | **224.40** |
| skew | `replace25` | 1024 | 2.49 | 1.49 | 160.3 | **0.0** | 179.6 | 882.2 | **1790.22** |
| tranco | `initial` | 16 | 2.01 | 1.01 | 2.4 | **0.0** | 1.5 | 4.0 | **31.89** |
| tranco | `initial` | 128 | 2.06 | 1.06 | 11.7 | **0.0** | 11.8 | 30.8 | **255.05** |
| tranco | `initial` | 1024 | 2.49 | 1.49 | 86.5 | **0.0** | 94.3 | 239.3 | **2035.61** |
| tranco | `insert25` | 16 | 2.01 | 1.01 | 2.2 | **0.0** | 1.3 | 14.8 | **28.70** |
| tranco | `insert25` | 128 | 2.05 | 1.05 | 9.5 | **0.0** | 9.4 | 118.1 | **230.14** |
| tranco | `insert25` | 1024 | 2.39 | 1.39 | 69.3 | **0.0** | 75.3 | 941.9 | **1836.30** |
| tranco | `replace25` | 16 | 2.01 | 1.01 | 2.4 | **0.0** | 1.5 | 6.6 | **30.21** |
| tranco | `replace25` | 128 | 2.06 | 1.06 | 11.7 | **0.0** | 11.8 | 53.4 | **241.78** |
| tranco | `replace25` | 1024 | 2.49 | 1.49 | 86.5 | **0.0** | 94.3 | 420.6 | **1930.11** |

### What each one says

| quantity | reading | conclusion |
|---|---|---|
| **start locates** | 2.01–2.49 per scan, where one suffices | real but small: `Index::locate` is re-evaluated inside `Index::scan`'s loop, so a scan across R regions pays R+1 searches instead of 1. **Measured, not changed this round.** |
| **empty groups** | **0.0 in every cell** | **the non-empty bitmap has no target here, so it is skipped** -- E9's empty-group distribution is not the E23/E26 state's |
| **sub-blocks skipped** | 1.3–179.6 per scan | tracks the records returned, not waste |
| **merge compares** | 0.23–0.99 per returned record | necessary work |
| **result allocations** | **1.75–2.00 per returned record** | the one worth changing |

The materialisation count is ~8× the merge comparisons and ~12× the group visits
per returned record, so it is the largest term that is not intrinsically needed.

### Per returned record, for the cells that matter

| dataset | state | n | groups / record | merge compares / record | **allocations / record** |
|---|---|---:|---:|---:|---:|
| mixed | `initial` | 16 | 0.234 | 0.232 | **1.997** |
| mixed | `initial` | 128 | 0.174 | 0.231 | **1.997** |
| mixed | `initial` | 1024 | 0.165 | 0.232 | **1.997** |
| mixed | `insert25` | 16 | 0.204 | 0.934 | **1.803** |
| mixed | `insert25` | 128 | 0.142 | 0.933 | **1.801** |
| mixed | `insert25` | 1024 | 0.132 | 0.932 | **1.800** |
| mixed | `replace25` | 16 | 0.234 | 0.645 | **1.904** |
| mixed | `replace25` | 128 | 0.174 | 0.643 | **1.904** |
| mixed | `replace25` | 1024 | 0.165 | 0.645 | **1.904** |
| skew | `initial` | 16 | 0.197 | 0.516 | **1.994** |
| skew | `initial` | 128 | 0.158 | 0.508 | **1.993** |
| skew | `initial` | 1024 | 0.157 | 0.509 | **1.993** |
| skew | `insert25` | 16 | 0.166 | 0.987 | **1.801** |
| skew | `insert25` | 128 | 0.126 | 0.985 | **1.798** |
| skew | `insert25` | 1024 | 0.127 | 0.984 | **1.798** |
| skew | `replace25` | 16 | 0.197 | 0.868 | **1.752** |
| skew | `replace25` | 128 | 0.158 | 0.866 | **1.753** |
| skew | `replace25` | 1024 | 0.157 | 0.865 | **1.754** |
| tranco | `initial` | 16 | 0.150 | 0.247 | **1.993** |
| tranco | `initial` | 128 | 0.091 | 0.241 | **1.993** |
| tranco | `initial` | 1024 | 0.085 | 0.234 | **1.993** |
| tranco | `insert25` | 16 | 0.134 | 0.926 | **1.794** |
| tranco | `insert25` | 128 | 0.074 | 0.923 | **1.798** |
| tranco | `insert25` | 1024 | 0.068 | 0.922 | **1.798** |
| tranco | `replace25` | 16 | 0.150 | 0.414 | **1.888** |
| tranco | `replace25` | 128 | 0.091 | 0.418 | **1.889** |
| tranco | `replace25` | 1024 | 0.085 | 0.412 | **1.890** |

## 2. The one change: one allocation per result instead of two

`prefix_ + std::string(suffix)` builds a temporary string and then concatenates
it. Reserving the result and appending in place returns the same complete
key/value pair with **one** allocation. Ownership, order and content are
unchanged -- verified, not asserted:

`check_scan` compares S0 and S1 output **element by element** on the same states
and starts, and *also* against the reference, so agreeing on a wrong answer is
not a pass. Every dataset: **9.5M–14.3M record comparisons per case**, `initial`
／`insert25`／`replace25`, including the short scans and the starts that fall
between two keys. `replace25` is included because it produces the empty middle
sub-block that E9's defect turned on.

## 3. Timing: S1 / S0, and both against the sorted array

Three arms in one process, rotated per repetition, paired inside a repetition.
`ARR` is a global sorted array with `lower_bound`, copying `n` records -- the same
return form as the prototype, so the comparison is like for like.

| dataset | state | n | S0 (ms) | S1 (ms) | ARR (ms) | **S1/S0** | **S1/ARR** | dir |
|---|---|---:|---:|---:|---:|---|---|---|
| mixed | `initial` | 16 | 8.63 | 7.34 | 5.07 | **0.855** | 1.457 | 5/5 |
| mixed | `initial` | 128 | 38.66 | 31.87 | 19.85 | **0.812** | 1.636 | 5/5 |
| mixed | `initial` | 1024 | 283.99 | 218.15 | 149.87 | **0.770** | 1.455 | 5/5 |
| mixed | `insert25` | 16 | 10.07 | 9.44 | 5.32 | **0.934** | 1.748 | 4/5 |
| mixed | `insert25` | 128 | 43.51 | 38.62 | 22.11 | **0.884** | 1.753 | 5/5 |
| mixed | `insert25` | 1024 | 305.16 | 268.83 | 160.94 | **0.882** | 1.634 | 5/5 |
| mixed | `replace25` | 16 | 9.80 | 9.01 | 5.40 | **0.907** | 1.656 | 5/5 |
| mixed | `replace25` | 128 | 48.74 | 40.61 | 23.24 | **0.813** | 1.770 | 5/5 |
| mixed | `replace25` | 1024 | 334.82 | 264.00 | 168.92 | **0.779** | 1.538 | 5/5 |
| skew | `initial` | 16 | 9.76 | 8.88 | 5.33 | **0.862** | 1.674 | 5/5 |
| skew | `initial` | 128 | 39.29 | 32.27 | 22.19 | **0.841** | 1.464 | 5/5 |
| skew | `initial` | 1024 | 280.10 | 223.21 | 164.78 | **0.784** | 1.296 | 5/5 |
| skew | `insert25` | 16 | 12.22 | 11.47 | 5.86 | **0.937** | 1.940 | 4/5 |
| skew | `insert25` | 128 | 41.78 | 37.15 | 22.52 | **0.897** | 1.651 | 5/5 |
| skew | `insert25` | 1024 | 290.70 | 246.97 | 179.04 | **0.858** | 1.412 | 5/5 |
| skew | `replace25` | 16 | 10.65 | 9.81 | 5.37 | **0.930** | 1.813 | 5/5 |
| skew | `replace25` | 128 | 40.37 | 35.88 | 21.41 | **0.877** | 1.684 | 5/5 |
| skew | `replace25` | 1024 | 283.15 | 240.41 | 172.92 | **0.851** | 1.395 | 5/5 |
| tranco | `initial` | 16 | 7.24 | 6.47 | 3.28 | **0.887** | 2.060 | 5/5 |
| tranco | `initial` | 128 | 34.11 | 26.60 | 12.64 | **0.770** | 2.057 | 5/5 |
| tranco | `initial` | 1024 | 232.32 | 176.89 | 83.48 | **0.758** | 2.128 | 5/5 |
| tranco | `insert25` | 16 | 10.28 | 8.92 | 4.28 | **0.851** | 2.092 | 5/5 |
| tranco | `insert25` | 128 | 43.01 | 37.30 | 17.43 | **0.843** | 2.100 | 5/5 |
| tranco | `insert25` | 1024 | 285.75 | 233.60 | 107.82 | **0.816** | 2.227 | 4/5 |
| tranco | `replace25` | 16 | 9.37 | 8.17 | 4.07 | **0.898** | 2.063 | 5/5 |
| tranco | `replace25` | 128 | 46.05 | 35.66 | 17.41 | **0.778** | 2.058 | 5/5 |
| tranco | `replace25` | 1024 | 301.34 | 229.91 | 105.53 | **0.768** | 2.184 | 5/5 |

Ratio below 1 = numerator faster. S1/S0 isolates the materialisation change.

**The scan is still slower than the plain sorted array.** E23 measured 1.28×–2.34×;
this table says by how much that narrows. A sorted array is a strong baseline for
sequential access and this round does not assume the gap must close.


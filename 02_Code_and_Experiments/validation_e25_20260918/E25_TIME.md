# E25 formal comparison: V0 / V1 / V2 / ART

Source: `results/main/e25_time.jsonl`, 3600 records.

**Ratios are PAIRED inside one repetition**, where all four arms ran the same
operation sequence from the same initial state, with the arm order rotated per
repetition. Each process's ratios are reduced to that process's median, and the
cross-process spread is reported. **No ratio here is the quotient of two
aggregate medians.**

**A ratio below 1 means the numerator is faster.** `dir` is how many independent
processes agree on that direction, over the processes that produced the cell.

The timed span contains the index operations **and everything they trigger** --
compaction, region rebuilds, container growth, string allocation. Key
generation, reference upkeep and answer checking run outside it. An arm that
disagreed with the reference aborted the run instead of recording a time.

`update_only` / `insert_only` / `replace` are **three separately built states**, 
each from its own fresh initial state. They are not a decomposition of one mixed
workload. `replace` reports cycles (one delete + one insert) and operations.

Every arm uses **M0, the existing maintenance rule**. No variant adds a
compaction or migration policy of its own.

## mixed

### update_only

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 1638 | 1638 | 1.95 | 0.75 | 0.78 | 0.79 |
| 10% | 3276 | 3276 | 3.56 | 1.51 | 1.63 | 1.63 |
| 25% | 8192 | 8192 | 8.36 | 3.63 | 3.86 | 4.05 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.410** | 0.369–0.432 | 5/5 |
| 5% | V2/V1 | **1.016** | 0.960–1.093 | 2/5 |
| 5% | V2/V0 | **0.411** | 0.383–0.480 | 5/5 |
| 5% | V2/ART | **0.957** | 0.890–1.020 | 4/5 |
| 10% | V1/V0 | **0.435** | 0.408–0.461 | 5/5 |
| 10% | V2/V1 | **1.016** | 0.944–1.192 | 2/5 |
| 10% | V2/V0 | **0.461** | 0.405–0.477 | 5/5 |
| 10% | V2/ART | **1.021** | 0.927–1.084 | 2/5 |
| 25% | V1/V0 | **0.445** | 0.409–0.448 | 5/5 |
| 25% | V2/V1 | **1.027** | 0.965–1.154 | 2/5 |
| 25% | V2/V0 | **0.451** | 0.423–0.470 | 5/5 |
| 25% | V2/ART | **0.971** | 0.895–1.086 | 3/5 |

### insert_only

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 1638 | 1638 | 0.73 | 0.69 | 0.63 | 0.41 |
| 10% | 3276 | 3276 | 1.43 | 1.26 | 1.16 | 0.78 |
| 25% | 8192 | 8192 | 3.89 | 3.40 | 2.89 | 2.21 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.962** | 0.886–1.030 | 4/5 |
| 5% | V2/V1 | **0.871** | 0.851–0.915 | 5/5 |
| 5% | V2/V0 | **0.869** | 0.829–0.922 | 5/5 |
| 5% | V2/ART | **1.567** | 1.348–1.660 | 0/5 |
| 10% | V1/V0 | **0.888** | 0.800–0.948 | 5/5 |
| 10% | V2/V1 | **0.906** | 0.871–0.952 | 5/5 |
| 10% | V2/V0 | **0.831** | 0.706–0.905 | 5/5 |
| 10% | V2/ART | **1.487** | 1.286–1.647 | 0/5 |
| 25% | V1/V0 | **0.903** | 0.767–0.926 | 5/5 |
| 25% | V2/V1 | **0.856** | 0.832–0.900 | 5/5 |
| 25% | V2/V0 | **0.732** | 0.680–0.800 | 5/5 |
| 25% | V2/ART | **1.311** | 1.281–1.383 | 0/5 |

### replace

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 1638 | 3276 | 0.96 | 0.92 | 0.83 | 0.55 |
| 10% | 3276 | 6552 | 1.93 | 1.89 | 1.71 | 1.14 |
| 25% | 8192 | 16384 | 4.78 | 4.47 | 4.13 | 2.32 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.954** | 0.924–1.014 | 4/5 |
| 5% | V2/V1 | **0.950** | 0.871–1.016 | 4/5 |
| 5% | V2/V0 | **0.908** | 0.901–0.927 | 5/5 |
| 5% | V2/ART | **1.523** | 1.493–1.746 | 0/5 |
| 10% | V1/V0 | **0.948** | 0.934–1.171 | 4/5 |
| 10% | V2/V1 | **0.961** | 0.785–0.996 | 5/5 |
| 10% | V2/V0 | **0.863** | 0.846–0.938 | 5/5 |
| 10% | V2/ART | **1.556** | 1.424–1.734 | 0/5 |
| 25% | V1/V0 | **0.931** | 0.856–0.985 | 5/5 |
| 25% | V2/V1 | **0.949** | 0.823–0.963 | 5/5 |
| 25% | V2/V0 | **0.820** | 0.805–0.883 | 5/5 |
| 25% | V2/ART | **1.728** | 1.691–1.833 | 0/5 |

## skew

### update_only

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 1638 | 1638 | 2.97 | 0.94 | 0.89 | 1.47 |
| 10% | 3276 | 3276 | 5.76 | 1.78 | 1.77 | 3.01 |
| 25% | 8192 | 8192 | 14.09 | 4.45 | 4.37 | 7.77 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.318** | 0.308–0.330 | 5/5 |
| 5% | V2/V1 | **1.011** | 0.848–1.178 | 2/5 |
| 5% | V2/V0 | **0.311** | 0.293–0.326 | 5/5 |
| 5% | V2/ART | **0.584** | 0.570–0.643 | 5/5 |
| 10% | V1/V0 | **0.326** | 0.307–0.332 | 5/5 |
| 10% | V2/V1 | **0.950** | 0.884–1.076 | 3/5 |
| 10% | V2/V0 | **0.304** | 0.289–0.336 | 5/5 |
| 10% | V2/ART | **0.598** | 0.551–0.688 | 5/5 |
| 25% | V1/V0 | **0.327** | 0.309–0.357 | 5/5 |
| 25% | V2/V1 | **0.976** | 0.949–1.103 | 4/5 |
| 25% | V2/V0 | **0.325** | 0.291–0.360 | 5/5 |
| 25% | V2/ART | **0.579** | 0.530–0.643 | 5/5 |

### insert_only

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 1638 | 1638 | 27.08 | 25.74 | 25.75 | 1.97 |
| 10% | 3276 | 3276 | 55.30 | 52.45 | 52.60 | 3.84 |
| 25% | 8192 | 8192 | 90.51 | 86.36 | 85.68 | 9.41 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.952** | 0.924–1.009 | 4/5 |
| 5% | V2/V1 | **0.985** | 0.921–1.003 | 4/5 |
| 5% | V2/V0 | **0.944** | 0.820–1.022 | 4/5 |
| 5% | V2/ART | **12.975** | 11.620–13.541 | 0/5 |
| 10% | V1/V0 | **0.975** | 0.898–0.995 | 5/5 |
| 10% | V2/V1 | **0.996** | 0.960–1.041 | 3/5 |
| 10% | V2/V0 | **0.962** | 0.914–0.966 | 5/5 |
| 10% | V2/ART | **13.689** | 12.189–13.890 | 0/5 |
| 25% | V1/V0 | **0.977** | 0.953–0.980 | 5/5 |
| 25% | V2/V1 | **0.988** | 0.952–1.026 | 3/5 |
| 25% | V2/V0 | **0.970** | 0.897–0.974 | 5/5 |
| 25% | V2/ART | **8.887** | 8.689–9.655 | 0/5 |

### replace

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 1638 | 3276 | 26.83 | 26.34 | 25.73 | 2.14 |
| 10% | 3276 | 6552 | 53.56 | 53.50 | 52.48 | 4.30 |
| 25% | 8192 | 16384 | 112.92 | 110.10 | 110.16 | 10.02 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.972** | 0.942–0.995 | 5/5 |
| 5% | V2/V1 | **0.971** | 0.918–0.992 | 5/5 |
| 5% | V2/V0 | **0.958** | 0.943–0.969 | 5/5 |
| 5% | V2/ART | **12.097** | 10.969–12.611 | 0/5 |
| 10% | V1/V0 | **0.997** | 0.972–1.007 | 4/5 |
| 10% | V2/V1 | **0.976** | 0.934–0.999 | 5/5 |
| 10% | V2/V0 | **0.963** | 0.951–0.975 | 5/5 |
| 10% | V2/ART | **12.215** | 11.605–13.304 | 0/5 |
| 25% | V1/V0 | **0.983** | 0.899–1.001 | 4/5 |
| 25% | V2/V1 | **0.986** | 0.922–1.016 | 4/5 |
| 25% | V2/V0 | **0.966** | 0.918–0.969 | 5/5 |
| 25% | V2/ART | **11.597** | 9.852–12.049 | 0/5 |

## tranco

### update_only

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 3276 | 3276 | 2.86 | 1.47 | 1.53 | 0.97 |
| 10% | 6553 | 6553 | 5.57 | 2.89 | 3.08 | 1.98 |
| 25% | 16384 | 16384 | 13.58 | 7.34 | 7.46 | 4.92 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.518** | 0.512–0.641 | 5/5 |
| 5% | V2/V1 | **0.997** | 0.958–1.094 | 3/5 |
| 5% | V2/V0 | **0.511** | 0.482–0.631 | 5/5 |
| 5% | V2/ART | **1.624** | 1.484–1.818 | 0/5 |
| 10% | V1/V0 | **0.534** | 0.508–0.607 | 5/5 |
| 10% | V2/V1 | **1.056** | 1.018–1.144 | 0/5 |
| 10% | V2/V0 | **0.569** | 0.542–0.589 | 5/5 |
| 10% | V2/ART | **1.576** | 1.410–1.743 | 0/5 |
| 25% | V1/V0 | **0.541** | 0.505–0.681 | 5/5 |
| 25% | V2/V1 | **1.039** | 0.934–1.070 | 1/5 |
| 25% | V2/V0 | **0.562** | 0.547–0.566 | 5/5 |
| 25% | V2/ART | **1.517** | 1.469–1.593 | 0/5 |

### insert_only

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 3276 | 3276 | 4.61 | 4.51 | 4.51 | 1.22 |
| 10% | 6553 | 6553 | 9.94 | 9.69 | 9.43 | 2.13 |
| 25% | 16384 | 16384 | 25.28 | 24.88 | 23.01 | 4.99 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.987** | 0.964–1.047 | 3/5 |
| 5% | V2/V1 | **0.973** | 0.824–1.083 | 3/5 |
| 5% | V2/V0 | **0.944** | 0.887–1.043 | 4/5 |
| 5% | V2/ART | **3.581** | 3.395–4.462 | 0/5 |
| 10% | V1/V0 | **0.999** | 0.946–1.058 | 3/5 |
| 10% | V2/V1 | **0.972** | 0.836–1.007 | 3/5 |
| 10% | V2/V0 | **0.927** | 0.901–1.013 | 4/5 |
| 10% | V2/ART | **4.249** | 3.656–4.920 | 0/5 |
| 25% | V1/V0 | **0.981** | 0.957–1.026 | 4/5 |
| 25% | V2/V1 | **0.963** | 0.872–0.988 | 5/5 |
| 25% | V2/V0 | **0.913** | 0.844–0.970 | 5/5 |
| 25% | V2/ART | **4.747** | 4.430–5.069 | 0/5 |

### replace

| checkpoint | cycles | ops | V0 | V1 | V2 | ART |
|---|---:|---:|---:|---:|---:|---:|
| 5% | 3276 | 6552 | 5.54 | 4.96 | 4.63 | 1.29 |
| 10% | 6553 | 13106 | 9.73 | 8.50 | 8.01 | 2.70 |
| 25% | 16384 | 32768 | 29.32 | 25.77 | 24.35 | 5.65 |

Cumulative ms through each checkpoint (median over all repetitions).

**Paired ratios** at each checkpoint:

| checkpoint | pair | cross-process median | range | dir |
|---|---|---:|---|---:|
| 5% | V1/V0 | **0.883** | 0.805–0.927 | 5/5 |
| 5% | V2/V1 | **0.934** | 0.889–1.002 | 4/5 |
| 5% | V2/V0 | **0.827** | 0.816–0.874 | 5/5 |
| 5% | V2/ART | **3.544** | 3.126–3.860 | 0/5 |
| 10% | V1/V0 | **0.889** | 0.874–0.912 | 5/5 |
| 10% | V2/V1 | **0.937** | 0.905–0.969 | 5/5 |
| 10% | V2/V0 | **0.827** | 0.801–0.858 | 5/5 |
| 10% | V2/ART | **2.918** | 2.713–3.001 | 0/5 |
| 25% | V1/V0 | **0.900** | 0.838–0.903 | 5/5 |
| 25% | V2/V1 | **0.936** | 0.910–1.000 | 4/5 |
| 25% | V2/V0 | **0.869** | 0.738–0.901 | 5/5 |
| 25% | V2/ART | **4.448** | 4.088–4.739 | 0/5 |

## Point-query regression

Fixed key sequence, timed outside the update span. Ratio below 1 = numerator
faster. This is the check that giving `find_impl` an optional out-parameter did
not move the read path.

| dataset | V0 | V1 | V2 | ART | V1/V0 | V2/V0 | V2/ART |
|---|---:|---:|---:|---:|---:|---:|---:|
| mixed | 7.09 | 7.21 | 6.98 | 5.11 | 1.01 | 0.99 | 1.39 |
| skew | 8.10 | 8.19 | 8.08 | 8.46 | 1.00 | 1.00 | 0.94 |
| tranco | 7.48 | 7.52 | 7.51 | 4.07 | 0.99 | 1.01 | 1.86 |

## Build time (context only -- building is not this round's subject)

| dataset | arm | median build ms |
|---|---|---:|
| mixed | V0 | 51.34 |
| mixed | V1 | 42.94 |
| mixed | V2 | 41.95 |
| mixed | ART | 35.03 |
| skew | V0 | 40.25 |
| skew | V1 | 39.96 |
| skew | V2 | 39.92 |
| skew | ART | 60.18 |
| tranco | V0 | 95.10 |
| tranco | V1 | 83.44 |
| tranco | V2 | 79.50 |
| tranco | ART | 34.77 |


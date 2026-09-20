# E26 long run: cumulative replacement to 200%, C1 vs C2

Source: `results/main/long_replace_{grow,same}.jsonl`, 768 records.

**The comparison is C1 vs C2.** The question is whether C2's benefit holds up
over a long run; the frozen version against ART is a different question, answered
elsewhere with a same-round ART arm.

**Workload.** Each cycle removes one live key and inserts a different one, so the
live count stays at ~N -- the task book's "活跃记录数基本不变". Checkpoints are
25% / 50% / 100% / 200% of N cycles. The index is spot-checked against the
reference after every segment; a drifted run aborts instead of recording a time.

**Two key modes, and why both are here:**

- `grow` -- the replacement carries a bounded suffix (~10 chars), so the key-length
  distribution drifts too. This is what inserting a genuinely *different* key does.
- `same` -- the replacement has the **same length** as the key it replaces and sits
  at the same place, so the key-length distribution is exactly the one the capacity
  plan was built for. **Only this mode can say whether space fails to stabilise
  under pure churn.**

## Mode `grow`

### mixed

| cycles (% of N) | live | C1 cum ms | C2 cum ms | **C2/C1** | dir | C1 overflow | C2 overflow | C1 compact | C2 compact | C1 MB | C2 MB |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| 25% | 131072 | 13.65 | 12.64 | **0.928** | 5/5 | 19657 | 19668 | 697 | 348 | 24.79 | 24.80 |
| 50% | 131072 | 27.97 | 25.40 | **0.928** | 5/5 | 43847 | 43860 | 793 | 396 | 28.91 | 28.91 |
| 100% | 131072 | 153.50 | 108.76 | **0.694** | 5/5 | 103348 | 103375 | 5126 | 2560 | 38.57 | 38.57 |
| 200% | 131072 | 282.67 | 221.43 | **0.775** | 5/5 | 129677 | 129703 | 10253 | 3081 | 42.24 | 42.24 |

### skew

| cycles (% of N) | live | C1 cum ms | C2 cum ms | **C2/C1** | dir | C1 overflow | C2 overflow | C1 compact | C2 compact | C1 MB | C2 MB |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| 25% | 131072 | 245.42 | 136.48 | **0.561** | 5/5 | 32318 | 32343 | 9134 | 4561 | 25.54 | 25.54 |
| 50% | 131072 | 342.96 | 198.14 | **0.582** | 5/5 | 63906 | 63946 | 13286 | 6635 | 29.95 | 29.95 |
| 100% | 131072 | 477.57 | 290.34 | **0.610** | 5/5 | 127642 | 127701 | 18252 | 9115 | 41.05 | 41.06 |
| 200% | 131072 | 699.61 | 464.17 | **0.657** | 5/5 | 128654 | 128704 | 36504 | 10421 | 41.31 | 41.31 |

### tranco

| cycles (% of N) | live | C1 cum ms | C2 cum ms | **C2/C1** | dir | C1 overflow | C2 overflow | C1 compact | C2 compact | C1 MB | C2 MB |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| 25% | 262144 | 92.20 | 68.46 | **0.734** | 5/5 | 33591 | 33650 | 6250 | 3361 | 27.11 | 27.10 |
| 50% | 262144 | 174.22 | 135.59 | **0.655** | 5/5 | 64602 | 64690 | 13211 | 7017 | 29.78 | 29.78 |
| 100% | 262144 | 407.79 | 277.24 | **0.694** | 5/5 | 126702 | 126872 | 26617 | 14024 | 35.14 | 35.16 |
| 200% | 262144 | 657.92 | 475.12 | **0.732** | 5/5 | 195241 | 195278 | 60022 | 20945 | 41.27 | 41.28 |

## Mode `same`

### mixed

| cycles (% of N) | live | C1 cum ms | C2 cum ms | **C2/C1** | dir | C1 overflow | C2 overflow | C1 compact | C2 compact | C1 MB | C2 MB |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| 25% | 131072 | 14.69 | 14.03 | **0.993** | 3/5 | 16743 | 16743 | 961 | 961 | 24.35 | 24.35 |
| 50% | 131072 | 63.62 | 62.19 | **0.991** | 3/5 | 39829 | 39829 | 1739 | 1739 | 28.17 | 28.17 |
| 100% | 131072 | 221.75 | 221.21 | **1.009** | 2/5 | 98564 | 98564 | 7318 | 7318 | 37.28 | 37.28 |
| 200% | 131072 | 349.51 | 334.18 | **0.927** | 5/5 | 128533 | 128533 | 14639 | 8962 | 41.13 | 41.13 |

### skew

| cycles (% of N) | live | C1 cum ms | C2 cum ms | **C2/C1** | dir | C1 overflow | C2 overflow | C1 compact | C2 compact | C1 MB | C2 MB |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| 25% | 131072 | 317.70 | 328.09 | **1.005** | 2/5 | 31724 | 31724 | 12272 | 12272 | 25.29 | 25.29 |
| 50% | 131072 | 422.53 | 425.50 | **0.993** | 3/5 | 62963 | 62963 | 18018 | 18018 | 29.42 | 29.42 |
| 100% | 131072 | 638.74 | 641.90 | **0.982** | 3/5 | 125996 | 125996 | 26930 | 26930 | 39.93 | 39.93 |
| 200% | 131072 | 889.97 | 813.00 | **0.874** | 5/5 | 127070 | 127070 | 53864 | 30877 | 40.07 | 40.07 |

### tranco

| cycles (% of N) | live | C1 cum ms | C2 cum ms | **C2/C1** | dir | C1 overflow | C2 overflow | C1 compact | C2 compact | C1 MB | C2 MB |
|---|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| 25% | 262144 | 130.76 | 130.54 | **0.997** | 4/5 | 23546 | 23547 | 5925 | 5920 | 26.18 | 26.18 |
| 50% | 262144 | 235.62 | 242.61 | **0.980** | 4/5 | 45164 | 45165 | 11018 | 11012 | 27.96 | 27.96 |
| 100% | 262144 | 502.82 | 531.88 | **1.019** | 1/5 | 85594 | 85595 | 22859 | 22830 | 31.30 | 31.30 |
| 200% | 262144 | 904.44 | 817.44 | **0.929** | 5/5 | 127053 | 127330 | 48344 | 35987 | 34.82 | 34.86 |

## Compaction work over the run (diagnostic build, counters reset per arm)

Counters are cumulative WITHIN an arm. The per-arm reset matters: without it arm
C2's figures are C1's plus its own, and C2 looks like it replays far MORE work than
C1 instead of less. Growth between checkpoints is what that quarter added.

| mode | dataset | arm | cycles | compactions | records replayed | copied (MB) | garbage (MB) | placements enabled |
|---|---|---|---:|---:|---:|---:|---:|---:|
| grow | mixed | C1 | 32768 | 697 | 25814 | 0.812 | 0.021 | 275 |
| grow | mixed | C1 | 65536 | 793 | 28616 | 0.901 | 0.024 | 315 |
| grow | mixed | C1 | 131072 | 5126 | 707999 | 32.992 | 0.230 | 918 |
| grow | mixed | C1 | 262144 | 10253 | 886101 | 42.329 | 0.276 | 1809 |
| grow | mixed | C2 | 32768 | 348 | 12647 | 0.398 | 0.021 | 166 |
| grow | mixed | C2 | 65536 | 396 | 14013 | 0.441 | 0.024 | 190 |
| grow | mixed | C2 | 131072 | 2560 | 352482 | 16.425 | 0.230 | 537 |
| grow | mixed | C2 | 262144 | 3081 | 368466 | 17.287 | 0.275 | 1057 |
| grow | skew | C1 | 32768 | 9134 | 1863815 | 77.014 | 0.375 | 955 |
| grow | skew | C1 | 65536 | 13286 | 2468266 | 116.917 | 0.646 | 1591 |
| grow | skew | C1 | 131072 | 18252 | 3256974 | 190.351 | 1.110 | 2353 |
| grow | skew | C1 | 262144 | 36504 | 3933695 | 236.704 | 1.283 | 4657 |
| grow | skew | C2 | 32768 | 4561 | 929190 | 38.392 | 0.375 | 555 |
| grow | skew | C2 | 65536 | 6635 | 1230156 | 58.260 | 0.645 | 913 |
| grow | skew | C2 | 131072 | 9115 | 1622984 | 94.832 | 1.109 | 1330 |
| grow | skew | C2 | 262144 | 10421 | 1669452 | 98.477 | 1.277 | 2634 |
| grow | tranco | C1 | 65536 | 6250 | 372820 | 5.056 | 0.103 | 1189 |
| grow | tranco | C1 | 131072 | 13211 | 765634 | 10.529 | 0.213 | 2592 |
| grow | tranco | C1 | 262144 | 26617 | 1686087 | 23.407 | 0.424 | 5054 |
| grow | tranco | C1 | 524288 | 60022 | 2515786 | 39.403 | 0.605 | 12666 |
| grow | tranco | C2 | 65536 | 3361 | 199628 | 2.692 | 0.103 | 954 |
| grow | tranco | C2 | 131072 | 7017 | 405653 | 5.546 | 0.212 | 2117 |
| grow | tranco | C2 | 262144 | 14024 | 883869 | 12.199 | 0.422 | 4213 |
| grow | tranco | C2 | 524288 | 20945 | 1042941 | 15.240 | 0.600 | 10874 |
| same | mixed | C1 | 32768 | 961 | 38035 | 1.161 | 0.029 | 464 |
| same | mixed | C1 | 65536 | 1739 | 274437 | 9.934 | 0.058 | 547 |
| same | mixed | C1 | 131072 | 7318 | 1136345 | 52.257 | 0.351 | 1509 |
| same | mixed | C1 | 262144 | 14639 | 1430534 | 66.152 | 0.430 | 3018 |
| same | mixed | C2 | 32768 | 961 | 38035 | 1.161 | 0.029 | 464 |
| same | mixed | C2 | 65536 | 1739 | 274437 | 9.934 | 0.058 | 547 |
| same | mixed | C2 | 131072 | 7318 | 1136345 | 52.257 | 0.351 | 1509 |
| same | mixed | C2 | 262144 | 8962 | 1206912 | 55.511 | 0.430 | 3018 |
| same | skew | C1 | 32768 | 12272 | 2568876 | 106.208 | 0.510 | 1478 |
| same | skew | C1 | 65536 | 18018 | 3384690 | 159.264 | 0.883 | 2429 |
| same | skew | C1 | 131072 | 26930 | 4948215 | 306.764 | 1.727 | 3817 |
| same | skew | C1 | 262144 | 53864 | 6041541 | 377.021 | 1.991 | 7635 |
| same | skew | C2 | 32768 | 12272 | 2568876 | 106.208 | 0.510 | 1478 |
| same | skew | C2 | 65536 | 18018 | 3384690 | 159.264 | 0.883 | 2429 |
| same | skew | C2 | 131072 | 26930 | 4948215 | 306.764 | 1.727 | 3817 |
| same | skew | C2 | 262144 | 30877 | 5104914 | 317.273 | 1.991 | 7635 |
| same | tranco | C1 | 65536 | 5925 | 639664 | 8.238 | 0.181 | 1861 |
| same | tranco | C1 | 131072 | 11018 | 1146096 | 14.900 | 0.340 | 3293 |
| same | tranco | C1 | 262144 | 22859 | 2623612 | 33.920 | 0.693 | 6700 |
| same | tranco | C1 | 524288 | 48344 | 4774126 | 60.631 | 1.155 | 9976 |
| same | tranco | C2 | 65536 | 5920 | 639087 | 8.231 | 0.181 | 1860 |
| same | tranco | C2 | 131072 | 11012 | 1145471 | 14.892 | 0.340 | 3292 |
| same | tranco | C2 | 262144 | 22830 | 2620262 | 33.879 | 0.693 | 6699 |
| same | tranco | C2 | 524288 | 35987 | 3811338 | 48.672 | 1.156 | 10084 |


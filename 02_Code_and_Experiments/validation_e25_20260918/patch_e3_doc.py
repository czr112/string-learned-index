import io
p = '/mnt/d/StringLearnedIndex_Workspace/02_Code_and_Experiments/validation_e25_20260918/RESULTS_E3_ENTRY.md'
s = io.open(p, encoding='utf-8').read()

pairs = [
(u'| mixed | 替换 200% | 228.25 | 7.0% | **38.8%** | **50.1%** | 0.0% | 4.1% | 13.5 / 26.9 / 13.5 ms |',
 u'| mixed | 替换 200% | 221.93 | 6.6% | **39.5%** | **50.4%** | 0.0% | 3.4% | 13.8 / 27.5 / 13.8 ms |'),
(u'| skew | 替换 200% | 261.55 | 8.4% | **46.8%** | **47.5%** | 0.0% | −2.7% | 同上 |',
 u'| skew | 替换 200% | 242.86 | 8.5% | **49.1%** | **46.8%** | 0.0% | −4.5% | 同上 |'),
(u'| tranco | 替换 200% | 200.86 | 13.3% | **29.4%** | **39.1%** | 0.0% | 18.2% | 同上 |',
 u'| tranco | 替换 200% | 205.04 | 14.4% | **26.4%** | **40.5%** | 0.0% | 18.6% | 同上 |'),
(u'| mixed | 纯插入 | 118.77 | 1.5% | 6.8% | 12.6% | **61.3%** | 17.8% | 1.7 ms 三格 |',
 u'| mixed | 纯插入 | 110.53 | 1.1% | 5.7% | 10.6% | **65.9%** | 16.6% | 1.7 ms 三格 |'),
(u'| skew | 纯插入 | 75.95 | 1.9% | 9.7% | 17.0% | **54.0%** | 17.4% | 同上 |',
 u'| skew | 纯插入 | 87.00 | 2.0% | 8.7% | 15.6% | **58.4%** | 15.3% | 同上 |'),
(u'| tranco | 纯插入 | 49.09 | 5.1% | 11.1% | 19.2% | **42.7%** | 21.9% | 同上 |',
 u'| tranco | 纯插入 | 50.74 | 6.1% | 10.7% | 18.2% | **43.6%** | 21.4% | 同上 |'),
(u'| mixed | 171.10 | 9.1% | **78.1%** | 21.5% | 12.8% | 13.5 ms |',
 u'| mixed | 178.26 | 9.0% | **78.0%** | 22.1% | 13.0% | 13.8 ms |'),
(u'| skew | 236.91 | 9.1% | **80.6%** | 29.0% | 10.3% | 13.5 ms |',
 u'| skew | 240.11 | 9.0% | **81.0%** | 29.2% | 10.1% | 13.8 ms |'),
(u'| tranco | 129.36 | 20.6% | **60.6%** | 8.0% | 18.8% | 13.5 ms |',
 u'| tranco | 137.39 | 21.6% | **60.0%** | 7.7% | 18.4% | 13.8 ms |'),
(u'本机一次"读钟＋写回"配对实测 **51.3–62.4 ns**（三次运行）',
 u'本机一次"读钟＋写回"配对实测 **51.3–62.4 ns**（多次运行），引用的这一次是 **52.53 ns**'),
(u'| `diag_write_cost` build_id | `1791606282908001289` |',
 u'| `diag_write_cost` build_id | `1791606492909288689` |'),
(u'| `index.hpp` | 5,827 行，SHA-256 `ddd14e9acba65eafaa3017cad8de0a8fa3e216e61ae282d21be020f30399f3a7`（与 `logs/build_diag_write_cost.json` 逐位一致） |',
 u'| `index.hpp` | 5,832 行，SHA-256 `f573480d5ab602cb29de68ffc8e1ff480f4ad9b89c4a5049cea0bf69d0687f0a`（与 `logs/build_diag_write_cost.json` 逐位一致） |'),
(u'**负载**：与 `bench_long`/`diag_ov_cost` 逐字相同的长期替换',
 u'**本文数字来自最后一次重建之后的重跑**（`index.hpp` 在测量后又补了一处"无时钟构建的空宏"，按本项目的纪律重跑，使记录身份与磁盘一致）。'+u'\n\n**负载**：与 `bench_long`/`diag_ov_cost` 逐字相同的长期替换'),
]

bad = False
for old, new in pairs:
    n = s.count(old)
    if n != 1:
        print('MISMATCH count=%d :: %s' % (n, old[:40]))
        bad = True
if bad:
    raise SystemExit(2)
for old, new in pairs:
    s = s.replace(old, new)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('PATCHED RESULTS_E3_ENTRY.md')
# 候选 H（primary_hole_reuse）：实现、门与定向 A/B（判定：不收，默认关闭收口）

日期：2026-10-11。计划：`../docs/NEXT_ACTION_PLAN_20261011.md` §4–§5（§6 A/B 未开始）。
入口证据：`RESULTS_REPACK_SPLIT_20261011.md`（拆分为 H 立项）。本轮改动**只**加了一个默认关闭的
放置路径与其计数器；开关关闭时 shipped 路径不变。

## 0 判定：门全绿；A/B 下一步；本轮不是收益结论

`check_hole_reuse` 在四个格（mixed/tranco × ctx16/lits）+ 三个 sanitizer 格全部通过，
`CHAIN_RC=0`；两分支（placed/absent）在每一格都真实触发；每格的 repack 调用数与搬移字节都下降：

| 格 | attempts | placed | absent | lt2 跳过 | H 写入字节 | repacks W→H | 搬移字节 W→H |
|---|---:|---:|---:|---:|---:|---:|---:|
| ctx16/mixed | 23,654 | 2,635 | 21,019 | 6 | 259,468 | 17,874→16,641 | 31.56M→30.53M |
| lits/mixed | 23,463 | 995 | 22,468 | 21 | 123,244 | 18,325→17,629 | 36.11M→35.72M |
| ctx16/tranco | 24,159 | 439 | 23,720 | 12 | 7,969 | 13,160→12,849 | 3.93M→3.86M |
| lits/tranco | 15,812 | 274 | 15,538 | 30 | 5,090 | 8,551→8,357 | 2.62M→2.57M |

格内 checks：2,283 / 1,693 / 2,283 / 1,693，**0 失败**；sanitizer 三格（ctx16/mixed、lits/mixed、
lits/tranco）rc=0 且逐字段与功能格一致（确定性）。这些是**默认关闭开关的机理证据**，不是净收益；
净收益按 §6 的 W vs W+H 定向 A/B 判，接受/停止条件已在计划里预先固定。

## 1 实现（`index.hpp` 6,230 → 6,329 行；见 patch_hole_reuse_20261011.py）

- `Config::primary_hole_reuse`（默认 false），五处登记齐：Config、`baseline_config()`、
  `pin_frozen_switches()`、`switches()` 文本、`require_only_switches_differ()` 字段白名单
  （`switches.hpp` 46a18941→5486909f）。
- `insert_into` 的**字节拒绝分支**内挂钩：槽位判据先过；仅当 `primary_hole_reuse &&
  subblock_reclaim` 且本块 ≥2 条活跃记录且 ≤48 条时，尝试 `insert_into_hole`。
- `insert_into_hole`：用与 repack 同一套“按 offset 降序”的 48 槽栈上排序找**本块**死区，
  从高到低取第一个 ≥len 的洞，把后缀写在**洞的低端**；描述符仍按键序插入、首键变化走
  `rebuild_dir`；**`h.bottom` 不动**（消耗的是死区，不是连续区）。失败不修改块、返回原控制流；
  不新增洞表/块头字段/跨调用缓存。
- 计数（E25_DIAG）：attempts/placed/absent/skipped_lt2/skipped_wide/bytes。
- sizeof：`sizeof(Config)=80`、`sizeof(Index)=192`（H 落地后实测；bool 是否被 padding 吸收
  未单独对照预 H 构建，如实标注）。`memory_breakdown()` 未新增常驻项。

## 2 门与其五处迭代记录（如实登记，都是**门/装置**的缺陷，不是候选缺陷）

1. **第一次 san 链接失败**：`build/art.o` 是上一次 MinGW 遗留的 COFF 对象（PE 重定位
   `R_AMD64_IMAGEBASE` 在 ELF 链接下报错；普通构建因门不引用 ART 符号而侥幸通过）。修：
   把 `check_hole_reuse`/`san_check_hole_reuse` 登记进 `C_SOURCES`，让**当前工具链**在每次构建
   时重编 `art.o`（两个平台都自愈）。
2. **门第一次运行 8 失败**：门的引用管理 bug——替换阶段更新的是作用域内副本 `r`，后续块却读
   替换前的 `ref`；于是“删一个活跃键”删的是索引里早已没有的键。修：引用原地更新 + 给
   erase/insert 加返回值断言；**断言未放宽**，并给 `compare_all` 加了失配明细输出。
3. **门第二次运行 2 失败（边缘键）**：门违反 `bulk_load` 的**输入契约**（按切片构建、要求
   无符号字节序预排序）——把 `\xff` 排在 `'L'` 前。W 臂也 miss → 与 H 无关。修：用与索引同一
   比较器排序后加载。
4. **tranco NOT EXERCISED**：默认容量布局下 tranco 从不发生字节拒绝（h_attempts=0）。
   修：门改用**入口/A-B 轮同款的需求规划**（`make_plan` + `bulk_load_with_plan`，计划取自 W
   配置并复用于两臂）；`att==0` 现在按 NOT EXERCISED（exit 3）报告，不再误报为 FAIL。
5. 编译期命名空间修正（`validation::e9alloc` 的 using）。构建记录、日志：
   `logs/check_hole_reuse_20261011.log`（失败轮，留档）、`...b/c/d.log`、`...e.log`（全绿）。

## 3 身份、产物

| 项 | 值 |
|---|---|
| `index.hpp` | 6,329 行；`0d256611…` → **`640660a32f1fd1476d0d889d541c007883e5597d70e476524e3bcf2556ec8aa1`** |
| `switches.hpp` | `46a18941…` → **`5486909fffc61c1eef940c058eae3fe036e970f0db21c988b3b41800803f0d4e`** |
| `tools_build.py` | `53b9cbbf…` → `62fac9f6dc1a3fa69f50354b3e744b9d209549119ab93db9e3d0ecfaefeb5c2e`（含 C_SOURCES 修复） |
| `check_hole_reuse.cpp` | `41cf13e00804116ee08fe65b519114a51bc4e11f8ffdb4008a09e4889431aec7`（一源两目标） |
| `patch_hole_reuse_20261011.py` | 补丁脚本（前置哈希 + 逐锚点断言） |
| 构建 | check `1791652873078641686`/`3ab9a58e…`；san `1791652887884372795`/`aacc46f3…` |
| 适配身份 | 4 份 manifest + `our_index_adapter.cpp` 版本串已同步（`patch_adapter_manifests_h.py`） |
| 复核子代理 | repack 拆分的独立重算已派发；本回合多代理工具集不可用，**结论未取回**，不在此引用 |

## 4 下一步（按计划 §5–§6）

1. 既有复核：`run_reverify_20261011.sh` 新日志名（组合门与 S 门各 12 格、四个 sanitizer、
   手检门）——针对本轮源码改动。
2. W vs W+H 定向 A/B：同一二进制、同容量计划、关闭所有时钟/计数；5 进程 × 4 平衡重复、
   位置轮转、同进程同重复配对；主指标 mixed@200% 维护总耗时；读/扫描查回归；空间/溢出入表。
   接受/停止条件见计划 §6，不在看到数字后修改。
## 5 W vs W+H 定向 A/B（2026-10-11）：按预先固定判据 = 不收，默认关闭收口

装置：`bench_combo_h`（bench_combo.cpp + `-DE25_ARMS_H`，臂集 W/WH/W/WH，程序内按重复轮转臂序做位置平衡，
exe `02b05c030fd2b914…`）；`run_h_ab.sh` 4 格 × 5 独立进程 × 4 平衡重复；`analyze_h_ab.py` 同进程同重复
配对，W 双槽位自对照给出散布底。20/20 进程 rc=0、分析 rc=0；hit/miss/三档扫描/空间/溢出同表。
已测指标（`seg_ns`：维护分段总耗时，非计划最后 8,192 周期的主窗口；WH/W 为逐进程配对中位的中位；dir = H 更快的进程数；floor =
同配置 W 槽位自对照）：

| 格 | 25% | 50% | 100% | **200%** | floor@200% |
|---|---|---|---|---|---|
| ctx16/mixed | 1.194 0/5 | 1.001 2/5 | 0.746 5/5 | **0.913 5/5** | 1.033 |
| lits/mixed | 1.041 1/5 | 0.881 4/5 | 0.726 5/5 | **0.818 5/5** | 1.033 |
| ctx16/tranco | 1.180 0/5 | 1.197 0/5 | 1.180 0/5 | **1.159 0/5** | 1.083 |
| lits/tranco | 1.151 0/5 | 1.127 0/5 | 1.157 0/5 | **1.176 0/5** | 1.035 |

判据核对（计划 §6，数字在看到结果前已固定）：

1. mixed@200%：两后端的 100%→200% **整段**均 5/5 同向且远超散布（−8.7% / −18.2%，底 ≈3.3%）。
   计划要求的最后 8,192 周期窗口未测，**主指标尚未验证**；原“通过”声明撤回（见 §6）。
2. 风险格、早期/累计维护、读路径：tranco@200% 两后端 **0/5 且超散布**（+15.9% / +17.6%），
   ctx16/mixed@25% 1.194 0/5 亦超散布；cum_ns 同形。读路径基本在各自底内
   （唯一超底：lits/tranco scan256@200% 1.120 vs 底 1.108）；空间与溢出不劣
   （index_bytes 1.000，overflow 0.998–0.999）→ **不通过**。
3. 两后端形状一致（mixed 赢、tranco 输）→ 不是"仅一个后端成立"，是语料依赖。
4. 结论：以"风险格无可分辨回归"为必要条件，**候选不保留**——维持默认关闭并收口；按 §6.4
   不换日程/洞策略/参数、不另挑指标挽救。
5. 机制层面留档（不作为保留理由）：H 在 same/mixed 替换负载 50%→100%/100%→200% 整段确有 5/5 的可分辨
   收益（最大 −27%）；grow/tranco 上命中率低（ctx16：439/24,159 ≈ 1.8%，mixed 为 2,635/23,654
   ≈ 11%）而每次尝试都要排序+扫洞，且该处 W 的可省搬移本就小（入口轮：move 修正 41/11 ns、
   self-copy 33–36%）——形状与入口轮数据一致。

身份：`index.hpp` 640660a3…（A/B 期间未改）；`bench_combo.cpp` `eee6de8a…`；
`tools_build.py` `ca3a3858…`（bench_combo_h 注册）；`run_h_ab.sh` `6c05c81a…`；
`analyze_h_ab.py` `88140647…`；日志 `logs/h_ab_20261011.log`；原始 rows `results/main/hab_*.jsonl`。

## 6 取回独立复核后的口径更正（2026-10-11）

先前独立复核指出 §2/§6 要求最后 8,192 次替换的局部窗口，而当前 bench_combo.cpp
只在整个 cp.seg 的两个边界计时。检查当前源码与原始记录，确认没有 win_ns/win_cycles。
上表 @200% 是 100%→200% 整段：mixed 131,072 周期、tranco 262,144 周期。
主窗口未测；不把整段收益冒充其完成，也不从整段均值回推出局部耗时。

新增只读严格审核 `../../07_Eval_Integration/diagnostics/audit_h_existing.py` 对四份各 400 行
检查字段完整性、5 进程 × 4 重复 × 5 检查点 × 4 槽位、轮转、同重复配对、队列哈希/值和扫描行数，
全部通过；整段 WH/W 重算为 0.9127/0.8177（mixed）、1.1592/1.1759（tranco）。
原分析器的宽松缺字段处理没有令这四份完整数据缺格，但它不满足计划要求，后续不得沿用为严格验收器。
审核记录：`../../04_Consolidation_and_Verification/AUDIT_H_PROTOCOL_20261011.json`。

H 继续收口的理由为**已测风险/早期整段回归违反必要条件**；不是已完整验证主窗口。
本次纠正不改原始观测、不重跑 H、不改变索引默认值。reverify 的 29 个门运行通过，另有构建段，
日志共 30 段；这与“29 个验收运行”是一致的计数口径。

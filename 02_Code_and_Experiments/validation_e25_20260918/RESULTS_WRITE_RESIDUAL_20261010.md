# 阶段 D：WIDE 后写路径残余成本入口诊断

日期：2026-10-10/11。计划：`../docs/NEXT_ACTION_PLAN_20261010_D.md`（阶段 D 第 3 步）。
装置：`diag_write_residual.cpp`（一源三构建：计数、计时、计时+目录探针）、
`run_write_residual.sh`、`analyze_write_residual.py`、`patch_stage_d_instrument.py`（补充核验）、
`patch_stage_d_switches.py`（开关纪律：补 S0 审计点名的三个 bool）。
原始记录：`results/write_residual_20261010b/`（本轮的 8 格工作表）、
`results/write_residual_locate_probe_20261010/`（目录探针）、
`results/write_residual_20261010/`（第 1 代，补充核验前；只用于"发现目标"）。

---

## 0. 判定：本轮入口不成立；三条被测结论，一条新的待测方向

按计划第 3 步，本轮的任务是在 W（`subblock_reclaim + reclaim_widest`，导航 C 与扫描 S 均
关闭）的**更新后状态**上指认一个仍可省工作的环节，并只实现一个候选。做完 8 格入口诊断、
一次补充核验（计划允许的一次）和一次目录探针之后，**计划第 5 节菜单上的四项没有一个通过**：

| 菜单项 | 本轮实测 | 判定 |
|---|---|---|
| 检索/放置重复定位 | 真实现象、量也可数（每插入约 5–11 次边界比较，其中约 2/3 是重算），但**每次走查本身在仪器的单调用分辨率地板附近**（`first_candidate` 每次 51 ns 原始值，而一个空计时对 54–79 ns） | **不立项**：可省量 ≤5% 插入时间，低于本装置能分辨的量级 |
| 放置失败后重复遍历 | 同上（同一条链的三个走查） | **不立项** |
| 重建占主导 | 本 8 格 `region_rebuilds = 0`、`threshold_splits = 0`（不触发分裂），`w_rebuild = 0` | **本工况不存在** |
| 溢出搬移占主导 | 只有 tranco 出现：ovR 1.60–3.18 条/擦除、ovB 77–153 B/擦除（占擦除的 3–6%） | **量级不足** |
| （额外测的）写路径用边界目录检索 | 探针两轮复现：插入侧 find −30%，但 place **+12%**、擦除侧 +2–6%，整周期净持平到 +3% 更差 | **不立项**（负结果，见第 4 节） |

而同一份工作表的**最大可动项不是菜单上的四项，而是回收本身**：W 状态下每次插入
0.98 次 `repack_subblock`（mixed），它占插入的 27%（316 ns），为了腾出约 119 字节而死区回收
要搬动约 5 KB 活跃后缀（`fail_bytes_live_sum` 中位数 5,018 B），并且**90% 的字节拒绝在失败
子块内已经存在 ≥ 请求长度的空洞**（`fail_bytes_hole_fits / place_fail_bytes = 0.90`）。
这条与 2026-09-27 关闭的"本块空洞复用"直接相关，但那条关闭理由的两个操作条件在 W 状态下
**都已被本轮数据推翻**（见第 5 节）。**下一步应是一次针对该环节的、有界的入口测试**，
而不是直接实现。

## 1. 装置与验收

8 格 = {Context16, LitsHpt} × {same/mixed 131072, grow/tranco 262144} × {25%, 200%}，
每格 1 进程 × 4 次重建重放；三个构建各跑满 8 格（探针只跑 ctx16 两格），全部 `rc=0`：

- **计数构建**（`-DE25_DIAG -DE18_COUNT`，无时钟）：逐替换周期在 erase 与 insert 之间取快照，
  每个计数**分到擦除侧或插入侧**；每个检查点同时给**区间增量**与**局部窗口**（最后 8,192 周期）。
- **计时构建**（`-DE30_TIME -DE30_WRITE`，无 E25 计数）：同样的逐周期边界，快照换成 E30 时钟。
  空计时对校准 53.9–79.0 ns/对，原始值与"减自身计时对"的修正值并列。
- **探针构建**（计时 + `-DDR_WRITE_LOCATE`）：写路径检索改走边界目录，用于给"目录检索"定价。

自检：每格 4 次重放的计数字段完全相同；`st_overflow` 与组合轮 W 臂逐位一致
（mixed@200% = 748、tranco@25% = 12,158），`st_index_bytes` 同为 23.97 MB / 26.51 MB；
逐键抽查与每轮末的全量有序对照（scan vs 参考 std::map）全过；插入/擦除被拒即抛错。

**一处身份说明**：第 2 代 8 格跑在 `switches.hpp` = `1e0bccc0…`（三个 bool 尚未显式化），
探针跑在 `46a18941…`（`patch_stage_d_switches.py` 之后）。两者**开关取值完全相同**，
差异只是显式化与 `switches()` 文本；行内 `switches` 串的差别即由此而来。

## 2. 计数工作表（窗口 @200%，per op）

| 格 | 回收/插 | 其中成功 | widen | compact | 落溢出 | 走查/插 | fc 走查 | 探针/插 | 尝试/插 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ctx16/same/mixed | 0.980 | 0.947 | 0.020 | 0.013 | 0.013 | 2.01 | 0.98 | 7.70 | 2.09 |
| lits/same/mixed | 0.992 | 0.959 | 0.023 | 0.010 | 0.010 | 2.03 | 0.99 | 21.13 | 2.12 |
| ctx16/grow/tranco | 0.459 | 0.290 | 0.008 | 0.161 | 0.161 | 1.63 | 0.46 | 2.93 | 1.68 |
| lits/grow/tranco | 0.248 | 0.165 | 0.004 | 0.079 | 0.079 | 1.33 | 0.25 | 2.23 | 1.36 |

字节拒绝的分母与去向（@200%）：mixed `fail_bytes` = 1.09/插，其中
`fits_hole` = 0.90、`repack_fits` = 0.90、`count_lt2` = **0.00**（拒绝从不落在 0–1 条记录的块上）、
`nholes` = 1 占 0.90；请求长度合计 132 B、本块死区 122 B、**repack 需要搬动的活跃字节 5,018 B**。
tranco：`fail_bytes` = 0.84/插，`fits_hole` = 0.37、`repack_fits` = 0.37、`count_lt2` = 0.00、
`nholes` = 0 占 0.63、活跃字节 370 B。

擦除侧（@200%）：full cmp 1.03–2.61、cmp 字节 20–136、描述符搬移 9.3–19.4、
释放主块字节 16.3–119.5；只有 tranco 出现溢出搬移（见 §0）。

## 3. 计时工作表（窗口 @200%，ns/op，原始值）

| 格 | 插入整段 | 定位(外) | find_located | place | 内含 repack | 走查 | 内含 fc | 残差 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ctx16/same/mixed | 1175 | 105 | 221 | 732 | 316 | 222 | 50 | 61 (5.2%) |
| lits/same/mixed | 1428 | 108 | 322 | 866 | 345 | 279 | 82 | 75 (5.3%) |
| ctx16/grow/tranco | 936 | 126 | 147 | 544 | 101 | 225 | 26 | 59 (6.3%) |
| lits/grow/tranco | 724 | 124 | 98 | 388 | 53 | 187 | 14 | 56 (7.7%) |

| 格 | 擦除整段 | 定位(外) | Region::erase | 内含 find_located | 残差 |
|---|---:|---:|---:|---:|---:|
| ctx16/same/mixed | 474 | 100 | 295 | 176 | 23 (4.8%) |
| lits/same/mixed | 513 | 112 | 312 | 205 | 31 (6.1%) |
| ctx16/grow/tranco | 494 | 142 | 262 | 149 | 31 (6.3%) |
| lits/grow/tranco | 441 | 134 | 218 | 115 | 30 (6.9%) |

`Region::insert_slow` 全格为 0（`reuse_locate_ctx` 的快路径覆盖了全部插入），
`w_reclaim` = repack、`w_walk` = put_primary 全段（含 insert_into）、`w_fc` = first_candidate。

**地板效应（本轮的关键限制，必须随数字一起引用）**：每个 `E30_W` 作用域自带一对时钟读数，
校准值 53.9–79.0 ns。`first_candidate` 的原始值是 51 ns/次（mixed）——**低于它自己的计时对**，
即"走查很便宜"这一结论在本装置上**不可分辨**；put_primary 每次 110 ns（含 insert_into）
也只能给出上界。任何"省掉几次走查"的候选，其收益都在本装置的分辨率之下。

## 4. 目录探针（写路径用边界目录检索）：可复现的负结果

`-DDR_WRITE_LOCATE` 把写路径检索切到边界目录（`mode.locate`），两次独立运行都给出同一形状：

| 格 @200% | find_located | place | 插入整段 | 擦除整段 |
|---|---:|---:|---:|---:|
| ctx16/mixed 目录关（第 2 代） | 221 | 732 | 1175 | 474 |
| ctx16/mixed 目录开（探针 1） | **152** | **823** | 1214 | 486 |
| ctx16/mixed 目录开（探针 2） | **157** | **826** | 1212 | 505 |
| ctx16/tranco 目录关（第 2 代） | 147 | 544 | 936 | 494 |
| ctx16/tranco 目录开（探针） | 138 | 534 | **912** | 504 |

插入侧的检索确实变便宜（mixed −30%），但**放置段同量级变贵**（+91 ns，两次复现），
擦除侧基本不动或略差。净效果：mixed 整周期 +3%，tranco −2.6%——**在 1 进程 × 4 重复的
噪声内，且方向不利于该改动**。这与冻结配置从未打开 `write_locate` 的历史一致，
本轮把它记为**实测负结果**：写路径的线性块扫描（每块一次标签过滤、命中即停）
不比"目录二分 + 单块扫描"差。

## 5. 下一件的入口证据：回收而不是走查

把 §2、§3 放在一起，W 状态下每次插入的写路径是这样的闭环：
**检索找到组 → 放置尝试撞上字节上限 → 回收（repack 该子块）→ 重试成功**。
回收几乎每次插入都发生（0.98），它搬动约 5 KB 活跃后缀只为腾出约 119 字节，
占插入的 27%。而 90% 的失败子块**本来就有 ≥ 请求长度的死区**（`fits_hole` = 0.90，
且 `nholes` = 1 占 0.90——死区是一段连续的洞）。

2026-09-27 的"本块空洞复用"关闭判定有两条操作理由，**在 W 状态下两条都已不成立**：

1. "200% 处每次只搬 39–139 字节，没有可重复搬运的东西"——W 状态下每插一次搬 **5,018 字节**
   （mixed），是旧读数的 36 倍；
2. "53%–98% 的拒绝落在只持有 0–1 条活跃记录的块上，`repack_subblock` 的 `n < 2` 守卫挡掉它们"
   ——W 状态下 `count_lt2 / fail_bytes = 0.00`，**拒绝从不落在 0–1 条记录的块上**。

第三条理由（"纯插入负载上没有总体"）与本轮无关：本轮的替换负载上总体是 1.09 次拒绝/插入。

**未测的、必须在实现前先测的一件事**：空洞放置要先把本块的死区找出来，这需要按 offset
排序一次（`repack_subblock` 的第一步也是同一次排序）。如果排序本身占据了回收 316 ns 的
大部分，那么"免搬移"能省下的就只有 memmove 那一段，收益会显著缩水。**下一步的入口测试
就是把这个比值量出来**（一次计时补丁或在探针里计时"排序+扫洞"vs"repack 全段"），
再决定是否立项。这条方向**尚未立项**，本轮不做实现。

## 6. 风险与未决

- §5 的方向一旦立项，验收必须同时含点查与扫描（沿用 A3 约束），且空洞放置会改变
  块内记录位置——扫描按描述符读、`repack` 按 offset 重排，两条路径都必须有门覆盖。
- 本轮的 1 进程 × 4 重复只用于**定位**：所有跨配置比较（探针 vs 基准）都受地板效应与
  单进程噪声限制，**不得当作 A/B 结论**。
- 第 1 代数据（`results/write_residual_20261010/`）绑定补充核验前的 index.hpp 身份。

## 7. 身份、交付与清理

| 项 | 值 |
|---|---|
| `index.hpp` | 6,124 → 6,170 行；`2bbcc23b…` → **`6aa6be5c8fd5d7a4c7fe9587b81308969f1d6ac7e26fbc24d46eba65365d0a49`**（补充核验补丁） |
| `switches.hpp` | `1e0bccc0…` → **`46a18941…`**（补 S0 审计点名的 `offset_directory`/`write_simd`/`write_locate` 进白名单与 `switches()`） |
| `vendor/lits_model_safe/lits_model.hpp` | **身份漂移（已登记）**：`b5a1c282…` → `4f8674b5…`，内容是把 `1UL << 52` 写成 `1ULL << 52` 的 LLP64 可移植性修正；在 LP64 上数值不变。改动由 `patch_lits_safety.py` 的一段断言式编辑固化（先验 E4 字节、再验树内字节）。本轮的 LITS 格与此后一切 LITS 结果都绑定在新字节上 |
| 装置 | `diag_write_residual.cpp` `51e1e8e1…`、`analyze_write_residual.py` `b5dd8bb1…`、`run_write_residual.sh` `b8e4e44c…`、`patch_stage_d_instrument.py` `a8715271…`、`patch_stage_d_switches.py` `ef293d4c…`；探针与复核脚本 `run_reverify_20261011.sh` |
| 构建记录 | `logs/build_diag_write_residual{,_t,_tl}.json`、`logs/build_check_combo.json` 等（复核后刷新） |
| 数据 | `results/write_residual_20261010b/*.jsonl`（8 文件 × 9 行）、`results/write_residual_locate_probe_20261010/*.jsonl`（3 文件） |
| 分析 | `logs/write_residual_20261010b_summary.log`（紧凑表）、`logs/write_residual_20261010b_analysis2.log`（全表） |
| 一次被正确拒绝的启动 | 补丁后第一次启动组合门时，12 格全部 `rc=2` 且拒绝理由写明"built from sources that have since changed … changed index.hpp / switches.hpp"——**装置按设计拒绝拿旧二进制充当新身份的复核**，日志随后删除（事实记在此行），重建后才跑出下一行的全绿结果 |
| 身份复核 | `run_reverify_20261011.sh` → `logs/reverify_20261011_d.log`：**29 段全 `rc=0`，0 失败**——组合门与 S 门各 12 格（3 语料 × 2 后端 × same/grow）、sanitizer 组合门与 S 门各 2 后端、手检门 1,675 项 0 失败（同一次复核也覆盖了 `patch_stage_d_switches.py` 之后的白名单） |
| 清理 | 三个子会话的临时物已删（工作区根的 `tmp_probe*`/`pe_*`/`copied_probe.exe`/`.tmp_*` 目录、`_wread*.py`、`patch_res_{targets,script_edit}.py`），以及一份**异工具链构建记录** `logs/build_diag_write_cost.json` 与其二进制（MinGW 编译，命令含 Windows 路径；E3 的原始数据仍在 `results/main/write_cost_diag_20261010.jsonl`）。两份环境核查件 `ENV_NATIVE_WINDOWS_PORT_20261010.md`、`ENV_HOST_VERIFY_20261010.md` 保留为证据 |
| 快照 | `04_Consolidation_and_Verification/MANIFEST_workspace_20261011a.sha256`：**3,133 项**；对上一代 `20261010f`（3,073 项）逐项比对 = **新增 62 / 删除 2 / 变更 18**（删除的两项是异工具链构建记录；变更 18 项 = index.hpp、switches.hpp、tools_build.py、patch_lits_safety.py、lits_model_safety.patch、vendor lits_model.hpp、4 份适配 manifest、适配器版本串、状态文档、两份计划、构建记录刷新）。树外重算逐项一致：**除清单自身外 0 增 0 删 0 变** |
| 相关 | `PRIOR_W_ENTRY_READ_20261010.md`（子会话独立重读组合轮 W 臂计数：回收 0.31–0.56/周期、1.7–1.9% 走到扩宽） |

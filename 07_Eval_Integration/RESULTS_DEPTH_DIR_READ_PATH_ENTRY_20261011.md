# 读路径入口诊断（P1）：目录的额外查询工作在哪里

日期：2026-10-11。装置：`diag_depth_dir_read_path`（新，读路径分段计数诊断；**不接计时**）。
依据：[截断目录之后：先补正确性，再限额诊断，回到结构优化](../../docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md) §4；
上游：[字节序修复](RESULTS_DEPTH_DIR_ORDER_FIX_20261011.md)、[TRUNCDIR 轮](RESULTS_DEPTH_DIR_TRUNC_20261011.md)。
原始记录：`results/main/depth_read_path_{mixed,skew,tranco}.jsonl`；运行日志 `logs/read_path_*.log`。

## 0. 判定

**目录读路径在本阶段收口；下一步转 M4 有序段归并。** 依据是四条互相独立、且互相约束的观察：

1. **额外工作来自目录搜索自身。** 静态命中每查询：模型段 `r.steps` 由 C16 的 7.75 / 14.86 / 3.21
   字节升到 DDirT 的 62.84 / 115.98 / 21.03 字节（mixed/skew/tranco），目录二分 8.27 / 8.28 / 7.50
   步、每步依赖上一次比较结果（arena 读）。这是计划中“只能确认二分有固定成本”的量化形态：
   成本是固定的每查询 ~8 步，随组数、不随查询分布改善而消失。
2. **指纹没有可独立消除的重复读取。** 两臂的 `fp_total` 逐语料**完全相等**（mixed 1,461,747；
   skew 1,444,297；tranco 218,472）；变化的只是融合位置——C16 在模型预测里融合 3.2–14.9 字节，
   DDirT 融合 0、tail 相应增加同量。因此不存在“把目录已读字节省回来”的独立机会。
3. **外层区域导航两臂逐项相同**（probes/查询 6.01 / 6.01 / 7.01；逻辑字节 180.7 / 451.1 / 10.8）：
   后端切换没有改变区域边界，这一格无优化对象。
4. **省下的工作集中在主块与更新后溢出，且只在长键语料里足够大。** 静态命中 subblocks/查询由
   11.31 / 16.86 / 2.68 全部降到 1.00；更新后命中 overflow 进入次数由 3.74 / 8.57 / 0.10 降到
   1.01 / 1.01 / 0.04。tranco 只剩 2.68→1.00 的余地，抵不掉 +17.8 字节的目录搜索——与
   TRUNCDIR A/B 的 tranco 回归（1.240）方向一致；skew 有 16.86→1.00 的余地——与 skew 胜利
   （0.830）方向一致。任何单一可部署改动都必须动目录搜索本身（步数/布局/编码宽度），属于几何或
   参数搜索；按计划 §4，该情形收口。**不新开原型。**

skew 作为场景候选的既有结论不因本诊断改变；上下文默认后端仍为 Context16。

## 1. 装置与断言

- 两臂 C16 与修复后的 DDirT；混合/偏斜 131,072 键、tranco 262,144 键（与计划同）。
- 探针：静态 20,000 条、种子 20261011、miss 10%；更新后 20,000 条、种子 20261012；按原 bench 的
  25/50/100/200% 批量擦除再插入日程构造状态。两臂各自的索引与本臂日程一致性由装置本身的相同调用
  序列保证；探针哈希见原始记录（`probe_hash` 字段）。
- `qmode{Interval, Prefetch::Off, fingerprint, simd, OvSearch::Hybrid, thr, locate=false}`：
  **断言 mode.locate==false 且每个查询 `locate_probes==0`**——路径是模型预测＋顺序标签探测，
  不是“模型预测＋子块目录二分”。绑定 `boundary_probes` 全表为 0。
- 影子规则：DDirT 每个预测真实执行的查询（`r.steps>0`），traced shadow 必须复现 `r.steps` 与
  `placement_group()`；命中值逐条对照独立 oracle，缺位键由 oracle 核实确实缺席。
- 计数构建：`-DE25_DIAG -DE12_COUNT`。新增计数点全部在诊断宏内：index.hpp 的 5 个读取路径
  `E25_CMP` 调用点改为 `E25_CMP_P/O`（非诊断构建展开为 `compare()`，与被替换代码逐字节一致）；
  外层导航计数、`fp_fused_bytes`、目录 trace 均在 `#ifdef E25_DIAG`。诊断后以 P0 门原生重跑
  验证出厂路径未被触动（§7）。
- 工具链：本机 MinGW g++ 15.2（本诊断只计数；无计时数字，不受工具链性能影响）。
  **逻辑比较字节不是缓存未命中或物理访存量；无 PMU，故不做分支/缓存归因。**

## 2. 断言与对账（全部为 0，或逐语料相等）

| 检查 | 结果 |
| --- | --- |
| 行级断言违例（mismatches / oracle / mode / trace / group / 总计分裂） | 0（全部 24 行） |
| `cmp_calls_total == exact_calls + ov_calls`（每查询） | 恒成立 |
| `fp_total` 两臂相等 | mixed 1,461,747；skew 1,444,297；tranco 218,472 |
| DDirT `dir_compared == model_bytes`（影子复现真实搜索） | 恒成立 |
| C16 `fp_fused == r.steps`（参考模型融合字节=模型步） | 恒成立（fused_ne_steps=0） |
| `dir_not_fitted` / `steps_zero` | 0 / 0 |

## 3. 分段账本（静态命中，每查询平均；全表见原始记录）

| 语料 | 臂 | 区域字节 | 模型字节 | 目录步 | 目录融合 | 尾部哈希 | 主块数 | SIMD 批 | 标签候选 | 主块比较字节 | 溢出比较字节 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| mixed | C16 | 180.7 | 7.7 | — | 7.7 | 73.5 | 11.31 | 11.31 | 1.28 | 85.1 | 3.2 |
| mixed | DDirT | 180.7 | 62.8 | 8.27 | 0.0 | 81.3 | 1.00 | 1.00 | 1.03 | 81.4 | 0.0 |
| skew | C16 | 451.1 | 14.9 | — | 14.9 | 65.5 | 16.86 | 16.86 | 1.53 | 84.9 | 4.4 |
| skew | DDirT | 451.1 | 116.0 | 8.28 | 0.0 | 80.3 | 1.00 | 1.00 | 1.02 | 80.5 | 0.0 |
| tranco | C16 | 10.8 | 3.2 | — | 3.2 | 8.9 | 2.68 | 2.68 | 1.16 | 12.2 | 0.0 |
| tranco | DDirT | 10.8 | 21.0 | 7.50 | 0.0 | 12.1 | 1.00 | 1.00 | 1.04 | 12.2 | 0.0 |

朴素逻辑字节合计（区域＋模型＋主块＋溢出）每查询：mixed 276.8 → 324.9（+17.4%）、
skew 555.3 → 647.6（+16.6%）、tranco 26.3 → 44.0（+67.6%）。**字节合计不解释 A/B 排序**
（skew 字节上升却更快），因为省下的是“块进入次数”这类字节计数看不到的访问；这也是
“未知什么”列出的部分。

## 4. 更新后与缺席（摘要；分层数据在原始记录）

- 更新后命中：subblocks/查询 C16 20.13 / 30.77 / 2.84 → DDirT 1.01 / 1.00 / 1.00；
  overflow 进入 3.74 / 8.57 / 0.10 → 0.00 / 0.01 / 0.04；目录搜索成本与静态相同
  （60.8 / 115.8 / 20.9 字节、8.27 / 8.28 / 7.50 步）。更新后 DDirT 的尾部哈希上升
  （mixed 89.1、skew 88.3、tranco 20.2 字节/查询）来自被更新键更长，两臂同量。
- 缺席：形态与命中一致（如 mixed 主块 18.91 → 1.00；skew 30.60 → 1.00）。
- 分布与区域：原始记录含 steps / subblocks / dir_probes 的 P50/P90/P99 与每区域 model_bytes 的
  P50/P99/max；键长维度由跨语料承担（mixed ≈118 B、skew ≈156 B、tranco ≈13 B）。
- 分母：静态每层 17,982 命中 / 2,018 缺席；更新后 18,052 / 1,948。所有“每查询”列以该层查询数为分母。

## 5. 少做什么 / 多做什么 / 未知什么

| | 内容 | 量化（静态命中，每查询） |
| --- | --- | --- |
| 少做 | DDirT 主块进入与标签过滤 | 块 11.31/16.86/2.68 → 1.00；SIMD 批同步下降；主块比较字节 −3.7/−4.4/−0.04；更新后溢出进入从 3.74/8.57/0.10 → ≤1.01 |
| 多做 | 目录二分（含依赖式 arena 读） | 逻辑字节 +55.1/+101.1/+17.8；步数 8.27/8.28/7.50；其中混合/偏斜有 1,465/12 次整阈值相等（`dir_end_sep_le_q`）而其余在首异字节定序 |
| 未知 | 目录步的真实物理代价 | 无 PMU：每步的缓存未命中、TLB、分支停顿不可归因；A/B 的 tranco 回归说明这 8 步的物理成本高于其字节数暗示的水平，但本诊断只能把这个差异标为未知 |

## 6. 与 TRUNCDIR A/B 的一致性

directory 的收益项与成本项在语料间此消彼长：收益随 C16 的主块进入数增长（长键语料组内块多），
成本几乎恒定（每查询 ~8 步、随组数缓慢变化）。于是 skew 净胜、mixed 未分辨、tranco 回归。
本诊断解释的是机制，不重开计时；A/B 的判定保持其在 [TRUNCDIR 结果](RESULTS_DEPTH_DIR_TRUNC_20261011.md)
中的原文与身份。

## 7. 交付、复现与身份

```bash
cd 02_Code_and_Experiments/validation_e25_20260918
python tools_build.py diag_depth_dir_read_path
python tools_build.py run diag_depth_dir_read_path mixed  131072
python tools_build.py run diag_depth_dir_read_path skew   131072
python tools_build.py run diag_depth_dir_read_path tranco 262144 ../validation_e5_20260913/data/tranco_XN6XN.csv
# 出厂路径回归（诊断后原生重跑；官方构建记录不动）
g++ -std=c++17 -O3 -march=x86-64-v2 -static -DE12_COUNT check_depth_dir_order.cpp -o build/_tmp_root_verify/check_depth_dir_order.exe && ./build/_tmp_root_verify/check_depth_dir_order.exe mixed 131072   # checks=132 failures=0
g++ -std=c++17 -O3 -march=x86-64-v2 -static -DE12_COUNT check_depth_dir_trunc.cpp -o build/_tmp_root_verify/check_depth_dir_trunc.exe && ./build/_tmp_root_verify/check_depth_dir_trunc.exe mixed 131072   # checks=1733 failures=0
```

| 文件 | sha256 |
| --- | --- |
| `validation_e25_20260918/index.hpp` | `645d66e27c05e6e070f61454377e8974b46b76fd694e62505d23f60486874488` |
| `validation_e25_20260918/model/equal_depth_dir.hpp` | `8cf2c311ed53b00d1cfc951dc19c82901c71752ab0ee74a814ebb7376c857f60` |
| `validation_e25_20260918/diag_depth_dir_read_path.cpp` | `e2b077e930f22d0a999dc58f9c079de039f584612527cdb813cb72602adac3a7` |
| `validation_e25_20260918/tools_build.py` | `f3bd2927a5e433d95316c7e00a96faf58a27462153470db596ddeb822c4bb240` |
| `results/main/depth_read_path_mixed.jsonl` | `f06d8b9c281862377a7294cbd1338004afe4278b5847da8fa6392f9fee1ff5e7` |
| `results/main/depth_read_path_skew.jsonl` | `ce3a702f2444dcf6389bcf75bb0eeef77f019a58788994363f9444229ac1c3c4` |
| `results/main/depth_read_path_tranco.jsonl` | `8940afcd319d6aa8fe2822d7f98ae6417f9528cd2ba670aed30e9beab31f441f` |

以上哈希为完成计数修复后（值拷贝）的三次运行记录；`logs/read_path_*.log` 为同次运行的完整控制台输出。

## 8. 边界与下一步

- 本轮只计数：不产生吞吐/延迟声明，不用整段时长相减冒充阶段成本。
- 目录后端保持默认关闭；本轮的 index.hpp 变更是**诊断计数的加性扩展**，出厂构建（无诊断宏）
  与 P0 身份字节等价（门重跑见 §7）。
- 下一步按计划 §6 恢复 **M4 有序段归并**：Context16 先验入口、≥50% 输出来自有效段、净比较减少 ≥30%，
  通过才做单个原型；不把本轮或目录结论带进 M4 的门槛。
- 若后续要研究“8 步固定成本”的物理侧，需要 PMU 与独立装置，单列；不在本轮范围。
# BM 主线：当前配置、总账与证据入口（阶段6简表，2026-10-19）

本表汇总已经落盘的结果。**当前判定以[最新审计](AUDIT_BM_NEXT_PLAN_20261019.md)及
[2026-10-19长期计划](../docs/LONG_TERM_BM_PLAN_20261019.md)为准。**
复核已撤回分块根的原入口通过结论，改正动态预算方向，并保存精确校验和复验。
生产默认仍是 **Context16**；本轮没有任何"已采用"的 BM 配置。

## 1. 身份

| 项 | 值 |
|---|---|
| benchmark | `003a55bf`（`07_Eval_Integration/upstream_snapshots/benchmark_003a55bf`） |
| 模型 | `3894c767338a865d2920ba5e21283827fea1d803`（`model_3894c767`，上游完整工厂可构建） |
| 编译器 / ISA | g++ 13.3.0，O3，`-march=x86-64-v3`，无 LTO |
| 页/槽位 | 2048 行/页，占位 15（16 槽），完整键校验 + CRC 指纹 |
| 输入 | 真实三类键（english_words / memetracker_urls / memetracker_phrases，各 65,536 键）+ tranco 风险格 |

## 2. 支持能力（BM 页，实验包）

| 操作 | 状态 |
|---|---|
| `bulk_load` | 原生；按页构建版只转换当前页（≤2048 行） |
| `find` | 原生；完整键验证，缺席安全纠错 |
| `scan` | 原生自持有；返回缓冲与索引不共享 |
| `insert` / `update` / `erase` | 页内完成，超槽/arena 时局部重建并换根 |
| `range_sum` | 不支持（明确抛错） |
| 并发 | 不支持（`concurrent_find=false`，单线程） |
| 只读候选（短目录） | 独立只读类型，仅 bulk_load/find/scan |

## 3. 最终读数（同轮配对，越小越好）

**查询**（完整 find，三类真实键 + tranco）：

以下是历史各轮的观察列，不是最新有界构建候选重新同轮测出的排名；不能跨行相除或相乘。

| 对象 | words | urls | phrases | tranco |
|---|---:|---:|---:|---:|
| BM 页 / ART（首轮） | 1.537 | 1.118 | 1.195 | 1.857 |
| 原生完整工厂 / ART | 1.586 | 1.335 | 1.234 | 1.903 |
| 短目录完整读 / ART | 1.362 | 1.032 | 0.975 | 1.573 |
| 自研 F5 / ART（控制） | 1.080 | 0.986 | 0.866 | 1.330 |

**空间与构建**（按页构建候选，首轮与泛化两轮）：

| 数据 | 常驻/ART | 构建峰值/ART | 峰值/旧全量复制 | 空间≤0.70 |
|---|---:|---:|---:|---|
| words（seed42 / 新种子） | 0.430 / 0.430 | 0.449 / 0.450 | 0.433 | 是 |
| urls（seed42 / 新种子） | 0.670 / 0.670 | 0.695 / 0.695 | 0.449 | 是 |
| phrases（seed42 / 新种子） | 0.729 / 0.731 | 0.754 / 0.756 | 0.471 | **否** |
| tranco/64k | 0.477 | 0.498 | 0.427 | 是 |
| words/100k（规模格） | 0.427 | 0.437 | 0.427 | 是 |

**动态**（阶段5入口与读预算）：

| 项 | 结果 |
|---|---|
| 逐操作正确性 | 四轨迹各20,000操作、失配0；实际仅find+insert增长；sanitizer两格 |
| 读预算 dynamic/snapshot | 正向重算0.981 / 0.970 / 0.948 / 1.003；观察中位数≤1.15，计时为终态现存键，不证明等价 |
| 根复制 | 每次发布复制当前整根；平均条目/初始页数=1.50–1.52；原合成批次不是通用平方复杂度证明 |
| 分块根入口（更正） | 标签错位；真实路由chunked/flat=2.063/0.873/1.992，原通过结论撤回。复制量降幅只是算式，未实现COW发布 |
| 暂停 | 单进程含oracle校验；80/20的p99约84/229µs，95/5约2.75/3.22µs；尚未分离根与页成本 |

## 4. 判定：什么可以用、什么不能用

- **不可用**：通用静态 `find`（bm 页 1.03–1.86×ART）；窗口纠正策略（真实数据四格清晰回归）；
  预测括号与 payload 探测（入口否决）；一层簇内前缀细分（URL fence 比较仅降 10.39%，未过 25% 门）；
  cost/adaptive（未分辨，构建预算未过）；短语的 ≤0.70 空间目标。
- **可用（场景/组件）**：短目录在 tranco/mixed 类键上的页内与完整读收益（但相对 ART 仍 1.03–1.57，
  且元数据超 5% 预算）；按页构建的空间/峰值收益（四格过组合门，短语空间 0.731）。
- **待重新筛选**：分块根的复制量估计有潜力，但真实短键路由约2倍，不能以原报告允许集成。
- **采用**：无。生产默认、动态 API、既有默认配置全部未改。

## 5. 证据入口（按阶段）

| 阶段 | 结果文档 |
|---|---|
| 1 完整上游参照 | [RESULTS_BM_NATIVE_20261018](RESULTS_BM_NATIVE_20261018.md) |
| 2 必要查询步骤 | [括号入口](RESULTS_BM_BRACKET_ENTRY_20261018.md)、[payload 探测](RESULTS_BM_PROBE_20261019.md) |
| 3 失辨与局部策略 | [编码审计](RESULTS_BM_ENCODING_AUDIT_20261019.md)、[局部细分](RESULTS_BM_LOCAL_REFINE_20261019.md)、[cost](RESULTS_BM_COST_20261019.md) |
| 4 静态与空间 | [空间账本](RESULTS_BM_SPACE_20261019.md)、[泛化](RESULTS_BM_SPACE_GEN_20261019.md)、[短目录完整读](RESULTS_BM_SHORTDIR_READ_20261018.md) |
| 5 动态与根 | [轨迹入口](RESULTS_BM_DYNAMIC_ENTRY_20261019.md)、[读预算](RESULTS_BM_DYNAMIC_READ_20261019.md)、[根规模](RESULTS_BM_ROOT_SCALE_20261019.md)、[根入口](RESULTS_BM_ROOT_CHUNK_20261019.md) |
| 早期 | [BM 首轮](RESULTS_BM_LATEST_20261018.md)、[真实键补测](RESULTS_BM_REAL_20261018.md) |

## 6. 复现

```bash
# 所有轮次都在 WSL 里，工作区根目录执行；输出目录必须尚不存在
bash tools/run_bm_native.sh bm_native_reference_new          # 阶段1
python3 -B 07_Eval_Integration/diagnostics/run_bm_encoding_audit.py --output bm_encoding_audit_new
python3 -B 07_Eval_Integration/diagnostics/run_bm_root_scale.py --output bm_root_scale_new
bash tools/run_bm_space_gen.sh bm_space_gen_new              # 阶段4 泛化
bash tools/run_bm_dynamic_entry.sh bm_dynamic_entry_new       # 阶段5 入口
bash tools/run_bm_dynamic_read.sh bm_dynamic_read_new         # 阶段5 读预算
python3 -B 07_Eval_Integration/diagnostics/run_bm_root_chunk.py --output bm_root_chunk_new
# 每个结果目录都带 independent verification：
python3 -B 07_Eval_Integration/diagnostics/verify_bm_<round>.py <结果目录>
```

## 7. 仍未完成

1. **评测身份硬化**：修计时臂绑定、精确整数、预算方向和失效断言，追溯规格/验证器版本漂移。
2. **实测根成本后决策**：保留FlatRoot；只有新根通过实测成本和完整读门才集成，旧分块原型不能直接接入。
3. **有限全操作动态对照**：先2数据×2比例×增长/替换8格，包含update/erase与ART/Context16；过门才扩24格。
4. **阶段6收口**：全部依赖从源码干净重建、正式包装完整门和配置取舍。已有同ELF与单格复验不能替代。
5. **遗留**：短语≤0.70空间目标未达；HOT泄漏未解；百万规模、区间扰动与缺席密集计时未覆盖。

## 8. 2026-10-20 更新（P0–P3）

- **P0**：[臂身份/校验和/预算方向](RESULTS_BM_P0_20261020.md)：分块根修正后 `chunked/flat`=1.968/0.884/1.924，入口判定撤回、FlatRoot 保留；读预算按 `dynamic/snapshot` 重算 0.948–1.003；“校验和差28”是 Number 往返误差。
- **P1**：[根复制只占维护批次 2.8%–7.4%](RESULTS_BM_P1_P3_20261020.md)，页构造+其余≈90%，故 **P2 新根机制不做**。
- **P3**：2数据×2比例×增长/替换八格全操作零失配；替换 1.16–1.47µs/次、**零页重建**，增长 13.4–38.4µs/次、102/381 次重建。
- **仍未完成**：P4（24格、集中写、scan、规模边界，需另立登记）；P5 的从零全依赖重建与正式接口复验；短语 ≤0.70 空间目标；HOT；百万规模与区间扰动。

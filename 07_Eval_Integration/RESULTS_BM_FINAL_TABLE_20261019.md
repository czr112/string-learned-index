# BM 主线：当前配置、总账与证据入口（阶段6简表，2026-10-19）

本表是**当前唯一入口**，替代此前分散的阶段小结。它只汇总已经落盘的结果，不引入新测量。
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
| 逐操作正确性 | 四轨迹 20,000 操作、失配 0；终态全键读回与全量 scan 一致 |
| 读预算 snapshot/dynamic | 1.020 / 1.031 / 1.055 / 0.997（≤1.15，且均在 AA 带内=未分辨） |
| 根复制 | 每次发布复制整根（条目/页=1.50–1.52）；1,000 插入累计条目 1,552→24,640（32→128 页） |
| 分块 COW 入口 | 发布条目降 43.8%/68.8%/81.3%、键字节降 46.1%/71.4%/82.4%；路由 1.005–1.027（≤5%） |
| 暂停 | p50/p95 0.55–3.3µs，p99 84–229µs，max 0.6–1.6ms（重建+根复制） |

## 4. 判定：什么可以用、什么不能用

- **不可用**：通用静态 `find`（bm 页 1.03–1.86×ART）；窗口纠正策略（真实数据四格清晰回归）；
  预测括号与 payload 探测（入口否决）；一层簇内前缀细分（URL fence 比较仅降 10.39%，未过 25% 门）；
  cost/adaptive（未分辨，构建预算未过）；短语的 ≤0.70 空间目标。
- **可用（场景/组件）**：短目录在 tranco/mixed 类键上的页内与完整读收益（但相对 ART 仍 1.03–1.57，
  且元数据超 5% 预算）；按页构建的空间/峰值收益（四格过组合门，短语空间 0.731）；
  分块 COW 的发布工作量降幅（入口过，**尚未集成**）。
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

1. **根机制集成**：把 16 键 COW 根接进 `Index`（真实页指针、发布/回滚、根内存口径），
   并重新过 15% 读预算——这是阶段5唯一剩余的机制项。
2. **24 格动态矩阵**：3 数据 × 2 读写比 × 均匀/集中写 × 替换/增长，含 ART/Context16 同能力对照。
3. **阶段6 收口**：干净目录重建复现、最终正式包装重跑完整门、少量正式配置。
4. **遗留**：短语 ≤0.70 空间目标未达；HOT sanitizer 泄漏未解（不排名）；百万规模、区间扰动与边界缺席未覆盖。

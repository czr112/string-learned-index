# P5：全依赖干净重建、正式接口复验与配置收口

日期：2026-10-20；执行入口[新长期计划](../docs/LONG_TERM_BM_PLAN_20261019.md)第 9 节。

## 1. 新目录、无历史对象的重建

在 `builds/p5_20261020/`（全新目录，无任何历史 `.o`/`.a`）从固定源码重建：

| 产物 | 做法 | sha256 |
|---|---|---|
| `libblocklearn_core.a` | 上游 CMake `blocklearn_adapter` 目标，Release、`-O3 -march=x86-64-v3`，benchmark contract 头 | `0619f1c2…d00b` |
| `libblocklearn_adapter.a` | 同上 | `2fb7c4ee…892d` |
| `art.o` | libart `art.c` 用固定源码与 `-DNDEBUG -O3 -march=x86-64-v3` 重编 | `aa9a89c5…d3f0` |
| `bm_page_acceptance` | 公共 contract 验收程序 + `bm_page_adapter.hpp` | `20ddad7a…1e67` |

- **libart 目标文件逐字节等于**空间轮记录的 `build/art_clean.o`（`aa9a89c5…`），说明该依赖的重建可完全复现。
- 两个上游静态库的哈希与空间轮记录的**不同**：本轮用 CMake 目标重建、路径与链接顺序不同。
  按计划，跨目录/跨构建的 ELF 不同不自动等于语义失败，也不因此声称等价；它们各自带新身份记录。
- 未复用任何历史库：本轮四个产物全部从源码产生。

## 2. 正式包装复验

用公共验收程序（`adapters/common/contract_acceptance.cpp`）对**正式适配包** `bm_page_adapter.hpp`：

```
100/100 checks passed
  memory = index_owned=1213 adapter_owned=16 retained_input=0
```

这是本会话第一次在同一轮里同时做到「依赖全新重建」+「正式 adapter 重过公共 contract」。
它验证的是接口/所有权/生命周期，不替代性能测量。

## 3. 最终配置表（收口）

| 配置 | 状态 | 依据与边界 |
|---|---|---|
| **Context16（动态生产默认）** | 默认，不变 | 完整功能、动态更新、既有评测基线 |
| **BM 页实验场景（只读，显式选择）** | 保留为实验 | 空间 0.427–0.731 ART、峰值 0.437–0.756、比旧全量复制低 53%–57%；读 1.03–1.86 ART；需显式选择，非默认 |
| **BM 页动态实验（页内槽 + 换根）** | 保留为实验 | 24 格全操作零失配；替换 0.84–1.67µs/次零重建、增长 10.6–35.9µs/次；读预算 `dynamic/snapshot` 0.948–1.003；无配对吞吐，不改默认 |
| 预测括号 / payload 探测 / 一层簇内细分 / cost/adaptive / 16键分块根 | **关闭** | 分别因入口门失败或（分块根）修正后短键约慢 2 倍；保留为研究线索，不集成 |
| 原生 BlockLearn 完整工厂 | 仅参照 | 读 1.19–1.90 ART；构建约 24–30 倍 ART；输入生命周期成本单列 |

**没有任何 BM 配置满足通用采用门**（每格 ≤1.10 ART）。因此最终结论是：
**限定场景 + 组件证据**，默认仍是 Context16；不升级为通用速度配置。

## 4. 仍未完成（不提前宣布）

- 多进程配对与吞吐排名（P3/P4 均为单进程）；
- 规模边界（>65k 唯一键）、集中写的真实日志验证；
- 短语的 ≤0.70 空间目标（0.731）；
- HOT 的适配器泄漏未修，不参与排名；
- 百万规模、区间扰动、边界缺席。

## 5. 复现

```bash
bash tools/p5_rebuild.sh                 # 全依赖重建 + 正式验收
php -v >/dev/null 2>&1 || true
```

产物与验收输出在 `builds/p5_20261020/`（不入库）；命令与哈希记录在本文件与
[results/bm_p5_20261020](results/bm_p5_20261020/)（`identity.json`、`acceptance.txt`）。

## 边界

- 「全依赖重建」指本轮四个产物；benchmark 的 `runner.cpp`/contract 头是按需编译的源码，不是预建库。
- 正式验收 100/100 只覆盖接口契约与所有权语义，不覆盖性能与全部语料语义门
  （后者见各轮的 corpus gates 与 sanitizer 记录）。
- 上游两库哈希与旧轮不同，属构建差异，不据此重写旧身份。

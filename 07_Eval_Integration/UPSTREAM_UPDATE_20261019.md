# 10月19日上游核对：模型更新至3894，核心未改，三工厂兼容通过

本次直接读取两仓库远端HEAD：

| 仓库 | HEAD | 本次变化 |
|---|---|---|
| zsh20070718/learned-index | `003a55bf674b43350fbb70242070fb74f33a56ce` | 与已用benchmark相同 |
| Zephyr761/Learned-Index-Model | `3894c767338a865d2920ba5e21283827fea1d803` | 旧为e8e05411；新提交时间2026-10-18 19:43:43 +08:00，`clear1018` |

独立快照：`upstream_snapshots/model_3894c767`，未覆盖e8e05411与历史依赖。
原模型URL可继续定位同一仓库，新快照未递归初始化有映射问题的related_works。

## 实质差异

27个文件变化，主要清理实验入口、文档与测试，benchmark子模块指针更新至003a55bf。
删除九个旧工厂/manifest，只保留：

- `block-residual-delta-shortdir-first`（旧目标参照）；
- `block-residual-delta-shortdir-first-cost`；
- `block-adaptive-delta-shortdir-first`。

保留工厂固定短目录和预测块优先能力，不允许参数把同名ID变为另一套开关配置；
adaptive的first表示允许选择该顺序，不表示每个叶都采用。
原BlockModel类仍在。14份被核对的核心/校准文件（BlockModel、LearnedIndex、planner、query_cost、
residual tags、key bytes、short directory、tag probe与calibrator）**逐字节相同**。
保留三工厂在本轮使用的空参数、instrument=1和非法开关拒绝上保持相同效果；
删除旧入口不构成本项目旧性能门通过，也不把上游“接受短目录”转写为本项目采用。

## 验证

新快照单独Release构建、统一使用现有benchmark003a55bf的contract与x86-64-v3：

- 上游 **CTest 11/11通过**；C++测试使用运行时check抛异常，不因Release/NDEBUG失去断言。
- 原生ART用工作区已固定的libart源码，仅通过CMake `ART_SOURCE_DIR` 引用；不改模型或原依赖。
- 新三工厂通过同一个修正生命周期的公共验收，并在words/URLs/phrases/tranco四格检查完整查询与scan；
  每种工厂的答案、工作计数、选择的hash/residual/first叶及容量，与e8e05411 cost轮普通记录**逐项相同**。
- 复用既有固定Release profile；无新增正式性能计时。新快照本次为Release兼容验证，
  不冒充重新做了新版本sanitizer；e8e05411原有sanitizer证据继续绑定原构建。

两次前置中止保留：a轮非登录WSL PATH没有cmake，构建前停止；
b轮缺ART原生依赖，CTest 10/11通过、ablation_contract因ART unsupported失败。
c轮使用已安装CMake的实际路径，并引用现有固定libart后11/11通过，没有禁用或跳过失败测试。
HOT原生依赖未配置，无HOT native测试/性能排名；旧适配包LSan泄漏状态不因这些Release检查改变。

## 后续版本边界

刚完成的[局部BM入口](RESULTS_BM_LOCAL_REFINE_20261019.md)和
[2000行cost/adaptive计时](RESULTS_BM_COST_20261019.md)仍明确绑定e8e05411，不能改成“新版重新测得”。
源文件相同、保留工厂兼容计数相同，支持继续按既有证据转空间路线；并非新版本性能等价证明。
后续阶段4使用3894c767作为新源码入口，既有重放脚本继续固定原快照；不能把已移除的旧工厂ID
直接带入新构建，也不改变历史失败与默认Context16。

证据：`results/upstream_update_audit_20261019c`，75份冻结来源、压缩完整diff/命令日志、
14份双版本核心哈希、CTest日志、新库/二进制哈希与四格原始兼容记录；`verification.json`为verified，
另已从原始文件独立复查全部来源/产物/归档哈希及四格记录相等。
首次PATH失败在`upstream_update_audit_20261019`，缺ART失败在`upstream_update_audit_20261019b`。

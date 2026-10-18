# 阶段1：上游完整 BlockLearn 参照（登记）

2026-10-18，起点`4b2ada2`工作树。依据[最新长期计划](../docs/LONG_TERM_BM_PLAN_20261018.md)阶段1与§5.2：
先接上游**完整工厂**的两个配置，与本项目BM页、F5、ART在同一输入上对照。

## 1. 唯一差分与命名

| 臂 | 实际实现 |
|---|---|
| `native-delta` | 上游完整工厂 `block-residual-delta`（`make_block_residual_delta`） |
| `native-shortdir-first` | 上游完整工厂 `block-residual-delta-shortdir-first` |
| `bm-page-window` | 本项目BM页 + 窗口内纠正（`bm_page::Index::find`） |
| `bm-page-base` | 同页 + 安全窗口扫描（`find_base`） |
| `F5` | 本项目冻结F5统一bridge（自研模型控制） |
| `ART` | 固定libart适配包 |

- 全部九臂使用**同一新contract**（benchmark `003a55bf/src/index_contract.h`），同一行/查询文件、
  同一单线程`find`接口。BM页/F5/ART按该contract重新编译，未改其源码。
- 上游模型与CMake目标`blocklearn_adapter`按快照源码原样构建，未改参数（fit `1,7,8,1`）与宏。
  该快照内`related_works`未初始化，因此上游自带ART/HOT基线不可用；ART由本项目适配包测量，
  HOT因未解的sanitizer泄漏继续排除。
- 每臂另有一次同实例AA（`native-delta_AA`等三对）。

## 2. 输入与负载

真实三类键（`english_words`/`memetracker_urls`/`memetracker_phrases`，各65,536键）与其冻结查询；
**每十条查询替换为已证明缺席的后缀键**（`~f2/miss`，与BM真实轮同形），约90%命中。
不使用合成tranco/skew，避免把旧风险格混进真实键结论。

## 3. 判据（登记）

- 正确性前置：九臂对**全部行与全部查询**必须与冻结答案一致，六臂校验和相等；任一失配即停止。
- 计时：5进程×6次Williams行，同repeat同置换；比值取每进程配对比值中位，再取5进程中位。
- 清晰差异：≤0.95或≥1.05、≥4/5同向、且超三对AA的最大观测带。
- 目标：`native-delta/ART`三格均≤1.10 才算达到通用静态读门的数值条件；
  `native-delta/bm-page-window`回答“完整工厂是否值得替代本项目接入”。
- 不做采用决定、不改默认、不接cost/adaptive、不跑12工厂笛卡尔积、不做动态操作。

## 4. 结果摘要（执行后填写）

`native-delta/ART`=1.704/1.372/1.352（三格均清晰更慢）；`native-shortdir-first/ART`=1.492/1.315/1.194；
`native-delta/bm-page-window`=1.038/1.104/0.987（均未分辨）；`sdf/delta`=0.846/0.944/0.900。
通用≤1.10门未过；完整工厂**没有**优于本项目接入，瓶颈是共有的预测/验证工作。
详见[执行结果](../RESULTS_BM_NATIVE_20261018.md)。

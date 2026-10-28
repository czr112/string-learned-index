# 最新原 BlockModel 接入及首轮窗口纠正结果

2026-10-18。**原 BM 已实际接入当前物理叶页；正确性和接口验收通过，首轮优化整体未通过。**
窗口内精确纠正在 skew 两态少约 22% 耗时，mixed 未分辨，tranco 更新后有清晰回归。
本轮 BM 六格均明显慢于自研 F5；尚未达到通用 ART 目标，生产默认保持 Context16。

## 被测版本与实现

- benchmark：[003a55bf](https://github.com/zsh20070718/learned-index/commit/003a55bf674b43350fbb70242070fb74f33a56ce)，10-17 21:56:49。
- 模型：[e8e05411](https://github.com/Zephyr761/Learned-Index-Model/commit/e8e05411135b222edb82e3861799a2ae07ae5470)，10-18 18:31:46。
- 被测索引由未经修改的上游 `blocklearn::BlockModel` 与本项目物理页、根导航和局部维护组成。
  实际链接 snapshot 的 `src/block_model.cpp`；fit 固定 `(block_size=1,bytes=7,segments=8,max_error=1)`。
  训练样本是稳定拥有的物理块 fence，排名直接对应块号。模型前缀视图借用页内保留 arena，
  页对象禁止移动/复制；换页转移拥有指针，插入不使训练字节重新分配。
- 保留 F5 的物理槽、CRC/完整键校验、稳定 fence、根发布、dirty 维护和失败回滚。
  不含自研 D1 模型训练或簇内 refinement；自研 F5 是独立控制组。
- 最新上游新增的短目录、predicted-first、cost/adaptive planner 尚未在这个物理页接入中启用。
  本轮不能代表这些完整工厂版本的性能。变更及接入条件见[上游核对](UPSTREAM_UPDATE_20261018.md)。

`BM_base` 是同一 BM 索引的候选窗口扫描路径，已经包含任意缺席键所需的安全 fence 检查。
它不是旧 BlockLinearRank，也不是完整 BlockLearn 工厂。`BM` 只改定位：在覆盖已验证的窗口内
用完整字节 fence 找精确前驱；提议窗口不覆盖查询时搜索已经证明的另一侧。两个路径共用完整查值校验。
`directory` 在同一 BM 对象上直接走完整 fence 目录。三个路径共享物理数据、模型状态及当前 map。

上游 `certified()` 的目标是训练误差预算；本配置真实页均未满足 max_error=1。
因此运行时始终把窗口当提议，使用完整字节边界纠错，不用训练标志冒充任意缺席查询证书。

## 验收与协议

普通 O2 和 ASan/UBSan 每种构建各 **13,152,655** 项检查通过，逐项输出一致。
覆盖空键、NUL/高位字节、长前缀、碰撞、边界缺席、随机增删改、完整删除、owning scan，
以及 8 个根准备失败场景、dirty 回滚和 65,536 个二进制查询。std::map、完整目录及冻结 F5 独立对照。
最新 v1 wrapper 普通/ASan 各 **99/99**，支持 bulk_load/find/insert/update/erase/owning scan，
range_sum unsupported，`concurrent_find=false`。

三语料分别 65,536/131,072/131,072 行；全库无放回查询，每十条构造一个已证明缺席的请求，约 90% 命中。
clean 为初建；dirty 固定 1024 次已有键 value update，最终 map 各实现相同，根发布数仍为 1。
ART 普通/完整 sanitizer 从固定源码重新编译，各 99/99；所有翻译单元使用同一个新 contract。
六格普通和 sanitizer 各完成 **18,075,640** 项比较预检后才计时。

预登记 [BM_LATEST_SPEC](diagnostics/BM_LATEST_SPEC_20261018.md)：5 进程 × 10 Williams 轮 × 10 臂 × 6 格，
共 **3000** 行；单 CPU0，O3、x86-64-v3、无 LTO，各策略同实例 AA，统一 v1 虚调用完整 find。
下表是先取每进程配对比值中位数，再取 5 进程中位数，**小于 1 表示 BM 耗时更少**。

## 同轮结果

| 数据/状态 | BM/BM_base | BM/目录 | BM/自研 F5 | BM/ART |
| --- | ---: | ---: | ---: | ---: |
| tranco64 / clean | 1.116 | 1.004 | 1.537 | 1.994 |
| tranco64 / dirty | 1.192 | 1.026 | 1.561 | 2.088 |
| mixed / clean | 0.957 | 0.968 | 1.234 | 1.309 |
| mixed / dirty | 1.004 | 0.989 | 1.241 | 1.389 |
| skew / clean | 0.781 | 1.047 | 1.238 | 1.079 |
| skew / dirty | 0.783 | 1.056 | 1.252 | 1.091 |

- skew 对 BM_base 两态均 5/5 更快，少 21.9%/21.7% 耗时，跨过 10% 及双方 AA 带，通过局部收益条件。
- mixed 两态均未形成清晰收益，没有通过预登记主目标；不把 clean 的 -4.3% 称作加速结论。
- tranco dirty 对 BM_base 多 19.2% 耗时，5/5 更慢且超 AA=1.069，风险门失败。
  clean 多 11.6%，虽 5/5 同向，仍在 AA=1.147 内，不能称清晰回归。
- BM 对 F5 六格均 5/5 更慢且超双方 AA：mixed/skew 多 23%–25%，tranco 多 54%–56%。
  这里的自研控制是 F5，不是旧 Context16 或 LITS。
- BM 对 ART 的 tranco/mixed 差距清晰；skew 数值多 7.9%/9.1%，在本轮 AA 内，未分辨。
  通用要求每格 BM/ART<=1.10 未满足，不能用 skew 两格掩盖其他数据。
- BM 与完整目录六格均未分辨；当前模型计算与窗内搜索尚未建立对目录的可分辨净优势。

**整体优化门失败，不采用这版“每次查询都窗内精确纠正”的通用策略。**
BM 继续作为模型研究主线；不以失败为由转回自研模型冒充 BM 优化。
没有改变判据或追加重复挽救结果，没有动态读写吞吐或默认采用结论。

## 工作量与空间边界

下面是本轮约 90% 命中正式查询的实际工作账，clean/dirty 相同。

| 数据 | 原路径进入块总数 | 纠正后进入块 | 纠正目录比较 | 窗口外提议次数 |
| --- | ---: | ---: | ---: | ---: |
| tranco64 | 669,440 | 65,536 | 273,320 | 3 |
| mixed | 1,686,700 | 131,072 | 586,184 | 5 |
| skew | 5,366,624 | 131,072 | 819,716 | 0 |

每条请求均调用原 BM，候选校验最后只进入一个块；目录比较和额外定位固定成本仍在。
访问块下降并未普遍转换为更低完整延迟。上述数值不同于语义门的 50% 命中探针，不混用两份分母。

| 数据 | BM 自计字节 | F5 自计字节 | BM/D1 静态自计空间 |
| --- | ---: | ---: | ---: |
| tranco64 | 2,183,791 | 2,231,007 | 1.056903 |
| mixed | 13,666,498 | 13,746,070 | 1.039843 |
| skew | 13,527,304 | 13,687,832 | 1.034715 |

这是容量和对象自计，排除 map/set 节点链接、分配器元数据和构建 scratch，不能作为完整堆峰值或对 ART 的空间排名。
新上游增加的统计字段带来每页 8 字节；两版语义门的 BM 调用、块访问和目录工作账完全相同，
这仅是固定输入上的版本兼容证据，没有对旧/新上游版本做性能排名。

## 失败记录、复核与下一步

第一次包含 HOT 的预检在固定 HOT 适配器的共用 sanitizer 验收中泄漏 96 字节，
调用栈在删除/重插分支；该次未开始任何计时。本轮在计时前另登记排除 HOT，未关闭 LSan，
也未改变外部算法强行通过。因此没有本轮 BM/HOT 排名。
原记录：[HOT 失败](results/bm_page_read_20261018/failure.json)。
更早的上游身份预检检测到历史 CRLF 哈希与当前 LF Git blob 差异，原记录亦保留；
实际源已按精确 Git blob 和字节哈希绑定，未改算法以匹配历史哈希。

下一候选优先使用最新 `locate().predicted`，先检查预测块是否形成完整 fence 前驱括号，
再按比较方向纠错，检验能否省掉每次固定的窗口端点检查和重复搜索。先登记结构工作入口，
通过后再跑完整读门；不把上游训练窗口当作缺席证明。短目录及 cost planner 各自另做消融，
cost 单位需按实际物理块标签批次换算。当前不扩 F6B/F6C 吞吐矩阵来掩盖读路径回归。

证据入口：

- [上游精确版本与逐文件哈希](results/upstream_update_audit_20261018/identity.json)；[benchmark 自带核心回归](results/upstream_update_audit_20261018/benchmark_regression.json)。
- [BM 语义复核](results/bm_latest_page_20261018/independent_verification.json)；[统一接口验收](results/bm_latest_adapter_20261018/verification.json)。
- [六格原始计时](results/bm_latest_read_20261018/raw.json)；[配对统计](results/bm_latest_read_20261018/summary.json)；[整体判定](results/bm_latest_read_20261018/decision.json)。
- [独立计时复核](results/bm_latest_read_20261018/independent_verification.json)：3000 行、顺序、checksum、AA、版本/输入/库/源码哈希及全部判定重新计算一致。

所有结果目录保留命令、实际源码快照和二进制哈希。生成脚本为 `diagnostics/prepare_bm_latest.py`，
执行脚本为 `run_bm_latest_page.py`、`run_bm_latest_adapter.py`、`run_bm_latest_read.py`；
复核为 `verify_bm_latest.py`。原依赖检出及历史实验均未覆盖，生产默认未改。

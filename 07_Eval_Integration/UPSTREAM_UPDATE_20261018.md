# 上游版本核对与本轮接入

2026-10-18 对用户指定的两个仓库执行 `git ls-remote`，并在独立目录检出精确提交。
原依赖检出、原实验源快照和历史结果均保留。版本及逐文件哈希见
[更新身份](results/upstream_update_audit_20261018/identity.json)。

| 职责 | 原固定提交 | 本次远端 main/HEAD | 提交时间（北京时间） |
| --- | --- | --- | --- |
| benchmark：[zsh20070718/learned-index](https://github.com/zsh20070718/learned-index) | 403de526 | [003a55bf](https://github.com/zsh20070718/learned-index/commit/003a55bf674b43350fbb70242070fb74f33a56ce) | 10-17 21:56:49 |
| 模型：[Zephyr761/Learned-Index-Model](https://github.com/Zephyr761/Learned-Index-Model) | 60837a88 | [e8e05411](https://github.com/Zephyr761/Learned-Index-Model/commit/e8e05411135b222edb82e3861799a2ae07ae5470) | 10-18 18:31:46 |

用户提供的 `Zephyr761/Learned-Index` 会重定向到 `Learned-Index-Model`，是同一模型仓库。
benchmark 新增 3 个提交，模型新增 2 个提交。完整提交、差分和统计保存在上述目录。

## 对当前工作的影响

- benchmark 增加混合 find/insert/update/erase/scan、静态并行读、热点/Zipf、原生参数传递和结果身份。
  原行/查询二进制格式兼容。`Capabilities` 新增 `concurrent_find=false`，须全部重编译，
  不能把旧 contract 编译的库与新头文件混合链接。BM 动态页仍单线程，不声明并发安全。
- 模型增加 `locate()` 返回预测块中心，原 `window()` 调用它，拟合及窗口的核心计算保持兼容；
  增加 tag-batch 统计、短目录、预测块优先探测、基于实际工作单位的 cost planner 及区域自适应验证。
  新优化以独立 factory ID 启用，旧 ID 不自动切换。cost 版本要求独立合成校准及匹配的目标/构建身份。
- 当前 BM 物理页接入使用最新原 `BlockModel`，固定 fit(1,7,8,1)，先检验窗口内精确纠正。
  短目录、predicted-first 和 cost planner 尚未同时启用，后续分别消融，不能把上游新功能都算作本轮收益。
  当前拟合样本的单位是物理块 fence，上游完整索引的规划样本单位是记录；接入 cost planner 前，
  必须按实际叶页标签批次重新计量，不能直接把 fence 样本的 `average_tag_batches` 当成查询成本。
- 最新 benchmark 自带 C++ runner 与 Python 核心回归两项均通过，旧 TLI 构建不在本次范围。
  见[回归记录](results/upstream_update_audit_20261018/benchmark_regression.json)。
- 新混合 runner 可供后续 F6B/F6C 使用；它当前只定义均匀动态选键，集中写矩阵仍须明确定义并保留实际轨迹。

## 本轮执行入口

[最新版本预登记](diagnostics/BM_LATEST_SPEC_20261018.md)绑定这两个提交。
原模型 `60837a8` 的普通/ASan 语义结果保留在 `results/bm_page_20261018`。
第一次 ART/HOT 比较在 HOT 共用 sanitizer 预检时出现 96 字节泄漏，尚未开始计时，
失败原样保存在 `results/bm_page_read_20261018/failure.json`；本次新协议预先排除 HOT，
完整比较 BM raw、BM 纠正、同布局目录、冻结自研 F5 与 ART，保留各自同实例 AA。
本轮现已执行完毕：[原BM接入、首轮优化及外部比较](RESULTS_BM_LATEST_20261018.md)。
正确性及接口通过，3000行独立复核；整体优化门失败，旧结果及失败记录保留。

后续每轮先核对远端提交；发生更新时另建身份和结果目录，先查接口与语义变化，再决定需要重跑的比较。
历史数据始终代表原固定版本，不能重标为“最新模型结果”。

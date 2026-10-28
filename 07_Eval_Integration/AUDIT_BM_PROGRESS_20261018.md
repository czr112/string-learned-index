# 最新BM结果的范围核对与计划约束

2026-10-18。核对[预测括号](RESULTS_BM_BRACKET_ENTRY_20261018.md)、
[短目录](RESULTS_BM_SHORTDIR_20261018.md)及[真实键补测](RESULTS_BM_REAL_20261018.md)，
用于制定下一步计划，不重跑或修改原实验。

## 可以使用的结论

- 预测前驱括号：主格fence比较1.118倍，入口关闭，没有性能计时；不再重复同一候选。
- 短目录：页内完整查值 short/current 为tranco 0.611、mixed 0.874，已有场景收益证据；
  根路由预先计算，不是完整索引find，不能推导ART排名。原五格结构门失败，不撤销。
- 真实键补测：每类65k，3000行与普通/ASan门已复核；窗口纠正泛化失败，原BM路径的短词差距仍明显。
- 上述bracket/shortdir的当前代码和二进制、输入哈希均匹配原身份；差异仅为已披露的规格结果段、
  短目录复核器修改。它们不等于原登记时的全部文本仍未变化。

## 短目录正式集成前必须补齐的口径

1. **空间不止每块8字节编码。** 当前 `bm_shortdir_core.hpp` 还持有
   `fence_views_`、用于计数的 `short_codes_` 和 `short_prefix_`。
   `memory()`只额外包含对象本身，没有统计这些动态容量；`short_directory_bytes()`只返回上游codes容量。
   在32页、每页137块、string_view为16B的tranco实验上，即使只按size估算，
   fence视图至少70,144B、计数副本至少35,072B，另有已列出的35,072B原生codes及4,352B对象增量，
   合计新增至少144,640B，而非只加35,072B；实际capacity、prefix heap及分配器尚需测量。
   正式候选应移除计数副本，给view容量和借用前缀生命周期完整记账，不能据旧摘要宣布空间门已过。
2. **完整fence比较需包含目录二分。** 原摘要使用的 `fence_compares` 字段主要是边界/相等码比较；
   current另有 `directory_steps`，short回退另有 `fallback_steps`，都是完整fence比较。
   同口径重算为(current boundary+directory)对(short field+fallback)：tranco两端点约0.0357/0.0348，
   mixed约0.4046，skew为1。原结构门仍失败，原0.114/0.381不能直接标作所有完整fence工作的比例。
3. **页内耗时含校验，根导航被排除。** 驱动调用 `page.find_model/find_short`，并非只测一条目录函数；
   完整索引需要恢复真实根导航、真实拥有对象、统一虚接口、缺席及更新状态，另开预登记。
4. **短目录命中会直接绕过BM预测。** 需报告模型调用和短目录比例；收益来自紧凑目录时按真实机制归因，
   不能直接称“BM拟合更准”或把页内比值乘到旧ART结果上。

## 模型参数含义

当前上游 `BlockModel::fit` 的 `max_error` 只参与 `certified_` 判定，拟合段radius在此前已经算完。
单独调小这个值不会收紧窗口。完整planner可能因认证结果选择其他方案，需要另行区分，
不能把这一参数当成直接收紧当前BM-page窗口的旋钮。

后续按[最新长期计划](../docs/LONG_TERM_BM_PLAN_20261018.md)，先补完整上游参照与短目录正式集成准备，
再做完整读门；成本选择及动态矩阵后移。

原始审计：[audit.json](results/bm_progress_audit_20261018/audit.json)。
v1容量增长估计及脚本保留；修订版改为不依赖vector增长倍数的下界，不改变实验输入、原始计时或原判定。

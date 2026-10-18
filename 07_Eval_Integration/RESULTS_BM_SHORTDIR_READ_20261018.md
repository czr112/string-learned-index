# 短目录完整读门：比现有页路径清晰更快，但仍未过 ART 与元数据门

日期：2026-10-18；起点`ff225d7`工作树。依据[最新长期计划](../docs/LONG_TERM_BM_PLAN_20261018.md)§5.3。
**结论：正式只读候选`shortdir`把完整find相对本项目`bm-page-window`降到0.832–0.887（四格全部清晰更快），
相对原生`-shortdir-first`为0.725–0.883；但对ART仍是1.362(words)/1.032(URLs)/0.975(phrases)/1.573(tranco)，
登记门（三类真实键均≤1.10）**未通过**，元数据5%门在words/tranco也**未通过**（7.87%/7.07%）。**

## 候选

`bm_sdindex::Index`（只读，自持有页与有序路由向量）＋每页`bm_sdpage::Page`（BM叶页＋上游`ShortDirectory`）：

- `find` 先走63位码有序路由，码相等才回读真实fence，括号不成立回退完整BM路径，再进同一完整键校验。
- 已**移除**入口轮的计数副本与本地码数组；新增常驻（8B/块码、稳定fence视图）**计入**候选容量。
- 非动态`FlatRoot`，是排序拥有向量；本轮只做只读门，无insert/update/erase。

## 全域门

四格各134,164项检查（1,545个边界探针＋全部行＋全部查询＋1,547次scan，含空键/NUL/高位字节/4KB键/
前缀/后继/缺口缺席，与`std::map`逐条比对）：**失配0**。words与phrases两格普通/ASan/UBSan
计数与内存完全一致。

## 完整find计时（5进程×6次，八臂）

| 输入 | shortdir/ART | shortdir/BM页 | shortdir/原生sdf | BM页/ART | 原生sdf/ART | F5/ART | AA带 |
|---|---:|---:|---:|---:|---:|---:|---:|
| english_words | **1.362** | **0.856** | **0.883** | 1.537 | 1.586 | 1.080 | 1.099 |
| memetracker_urls | 1.032 | **0.887** | **0.725** | 1.118 | 1.335 | 0.986 | 1.208 |
| memetracker_phrases | 0.975 | **0.832** | **0.816** | 1.195 | 1.234 | **0.866** | 1.087 |
| tranco/64k（对照） | **1.573** | **0.841** | **0.793** | 1.857 | 1.903 | 1.330 | 1.081 |

每请求ns（shortdir / BM页 / 原生sdf / ART / F5）：words 204/225/235/152/172；
URLs 318/364/424/316/296；phrases 338/400/406/344/304；tranco 183/220/229/118/156。

## 判定

1. **登记门未过**：三类真实键需均≤1.10，实测words 1.362（5/5更慢）与tranco 1.573；
   只有phrases到0.975（未形成清晰收益，2.5%<5%门）。因此**不进入采用**，也不改门。
2. **元数据门未过**：`short_directory`35,072B＋`fence_views`131,072B 占候选总量7.87%(words)、7.07%(tranco)、
   2.62%(URLs)、2.31%(phrases)。视图向量按capacity计且发生一次翻倍（实际需4,384个view，容量8,192）；
   显式`reserve`可减少这部分，但本轮的候选已按登记形态测量，**不在事后改装**。
3. **机制本身有效**：对被替换的`bm-page-window`四格0.832–0.887（全部5/5并超AA带），
   对上游自带`-shortdir-first`为0.725–0.883。说明“紧凑码路由替代随机arena fence比较”是可复用的一步，
   但单靠它不能把BM结构带到ART水平。
4. **剩余差距仍在模型/预测侧**：F5（自研模型）在phrases清晰快于ART、URLs接近持平，
   而BM页两版都明显更慢。这与阶段1“完整工厂不优于我们接入、差距在共有预测/验证工作”的结论一致。

按计划停止规则：记录并停止该形态作为通用配置；短目录保留为**组件级证据**（页内+完整find均成立），
下一入口转向阶段2的“必要查询步骤”——预测块payload探测，或按阶段3在确证失辨后做局部细分；
不再调整编码宽度/段数/槽位来挽救本轮。

## 边界与披露

- 只读：无动态操作、无clean/dirty两态；静态根是排序拥有向量，不是动态`FlatRoot`的发布语义。
- HOT仍因未解sanitizer泄漏缺席；原生臂为上一轮构建的同一库（哈希已入账）。
- 输入为命中+10%缺席（~90%命中），非真实访问分布；65k不外推百万级。
- 全库scan只在全域门里各做一次，未做扫描性能比较。
- 一次中止：无（本轮首次运行即通过全域门与计时）。复核器与规格为“执行后填写”允许变更项，
  其运行哈希与当前哈希都记录在`verification.json`。

## 证据

- 规格：[BM_SHORTDIR_READ_SPEC_20261018.md](diagnostics/BM_SHORTDIR_READ_SPEC_20261018.md)
- 原始记录：[results/bm_shortdir_read_20261018](results/bm_shortdir_read_20261018/)：
  `domain.json`、`domain_sanitized.json`、`raw.json`(960行)、`summary.json`、`decision.json`、
  `identity.json`(35份源码/输入/产物哈希)、`verification.json`、`commands.log.gz`。
- 复核：`python3 -B 07_Eval_Integration/diagnostics/verify_bm_shortdir_read.py <结果目录>` → `verified`。

# benchmark 真实键补测：BM原路径、窗口纠正与控制

2026-10-18。**真实数据补测和3000行独立复核已完成。每次都做完整fence纠正的候选不适合继续采用。**
原BM接入基本路径在MemeTracker URL上数值接近ART，在短词表上仍有约48%–50%耗时差距。
本轮证明了当前接入路径的边界，尚未测最新完整BlockLearn工厂，不能据此判定其性能上限。

当前后续顺序见[详细长期计划](../docs/LONG_TERM_BM_PLAN_20261018.md)。
同时纳入已落盘的预测括号关闭及短目录页内收益；其范围与记账限制见[结果口径核对](AUDIT_BM_PROGRESS_20261018.md)。

## 数据来源与准备

按benchmark `003a55bf` 的 `datasets.lock.json` 和未修改的 `prepare_real_datasets.py`，
完整扫描后以bottom-k BLAKE2b哈希抽唯一键，seed=42，各65,536键。
**两份原始来源、三类真实键**：词表一份；URL和短语来自同一个MemeTracker phrase-cluster文件。
这不是TLI 90M URL数据，也不代表真实查询日志。

| 数据 | 候选记录（含重复） | 键长中位 / p95 / 最大 | 相邻LCP中位 / p95 | 样本高位字节键 |
| --- | ---: | --- | --- | ---: |
| english_words | 466,550 | 9 / 15 / 27 B | 4 / 9 B | 0 |
| memetracker_urls | 7,665,108 | 75 / 137 / 471 B | 32 / 67 B | 23 |
| memetracker_phrases | 310,457 | 48 / 253 / 3729 B | 9 / 26 B | 0 |

词表扫描466,550行、4,862,985字节；MemeTracker扫描8,357,595行、解压后862,128,572字节。
原脚本仅移除LF/CRLF并按原字节排序去重；短语原文中13条超4096B记录按脚本既定规则排除，
其余两类没有空/NUL/超长过滤。保留空格、标点及高位字节。
来源URL、版本、完整哈希、过滤计数、选样哈希见[输入身份](results/bm_real_20261018/identity.json)。

首次下载超过原下载器300秒限时，英文样本保留；MemeTracker随后下载相同锁定文件并核对SHA-256，
完整内容的native hardlink发布被拒，重新核对后用同目录rename发布，再由原脚本离线取样。
这些准备失败不涉及性能数据，全部记录保留，未换来源、缩减样本或跳过完整扫描。
原始数据与派生样本位于忽略的snapshot/data目录，不随代码重新分发。

## 实现、校验与固定协议

使用和上一轮相同的四个普通/sanitizer二进制，逐字节核对SHA-256，所有实际源码归档。
模型仍为 `e8e05411` 的原BlockModel，fit(1,7,8,1)，原物理页、根、CRC及算法未改。
上游最新短目录、预测块优先与cost/adaptive完整工厂未在此轮启用。

BM_base是原模型窗口扫描的安全接入路径；BM是每次做窗口内完整fence精确纠正的候选。
directory与这两臂共用一个BM对象，F5为自研控制；ART是真实固定工厂，所有查询走同一v1接口。
各实现使用同一当前map，clean为初建，dirty为原固定seed的1024次已有值update，根发布数仍为1。

每份新语料普通/ASan corpus完成增删改、scan、compact及独立std::map/F5对照，每种构建合计
**6,634,925**项；六格普通/ASan全部实现逐键、查询、完整scan预检，每种构建合计 **10,817,354**项。
所有预检通过后才计时，未关闭LSan。HOT固定适配器的既有泄漏未解决，本轮不排名。

预登记[固定协议](diagnostics/BM_REAL_SPEC_20261018.md)：源键全量一次shuffle(seed=20261018)，
每十条追加一个已经证明缺席的后缀请求，约90%命中；5进程×10轮×10臂×6格=3000行。
CPU0、O3/x86-64-v3、无LTO，同实例AA、Williams顺序、逐轮checksum均沿用上一轮。
这是benchmark真实数据准备流程和统一接口上的同轮诊断，未声称完成其全部访问分布/动态suite。

## 主要配对结果

比值小于1表示分子耗时较少。先对每进程10个配对比值取中位数，再取5进程中位数。

| 数据 / 状态 | 窗口纠正BM / BM_base | 窗口纠正BM / 目录 | 窗口纠正BM / F5 | 窗口纠正BM / ART |
| --- | ---: | ---: | ---: | ---: |
| 英文词表 / clean | 1.164 | 1.028 | 1.585 | 1.759 |
| 英文词表 / dirty | 1.168 | 1.040 | 1.653 | 1.720 |
| MemeTracker URL / clean | 1.166 | 1.058 | 1.313 | 1.256 |
| MemeTracker URL / dirty | 1.102 | 1.014 | 1.266 | 1.185 |
| MemeTracker 短语 / clean | 1.138 | 1.034 | 1.468 | 1.261 |
| MemeTracker 短语 / dirty | 1.098 | 1.005 | 1.430 | 1.249 |

窗口纠正相对BM_base六格均5/5更慢；词表两态、URL clean、短语clean共四格超双方AA形成清晰回归。
URL dirty与短语dirty虽数值多10%左右，但未超AA带，记未分辨。与完整目录六格均未分辨。
窗口纠正版对F5、ART六格均清晰更慢，所有BM/ART数值目标<=1.10都未过。

因此停止将“所有查询都精确收窄到一个块”作为通用优化策略；不能用合成skew的一格收益挽救此轮。
上一轮mixed/tranco失败和整体不采用判定也不改变。

## 原BM路径与控制的补充直接配对

下面在同一冻结raw上直接重算配对，属于运行后的补充描述，不是新采用门；
没有相乘中位数或重跑性能，见[补充计算](results/bm_real_20261018/additional_comparisons.json)。

| 数据 / 状态 | BM_base / ART | BM_base / F5 | BM_base / 目录 | F5 / ART |
| --- | ---: | ---: | ---: | ---: |
| 英文词表 / clean | 1.496 | 1.339 | 0.848 | 1.105 |
| 英文词表 / dirty | 1.478 | 1.387 | 0.868 | 1.097 |
| MemeTracker URL / clean | 1.062 | 1.110 | 0.897 | 0.962 |
| MemeTracker URL / dirty | 1.069 | 1.144 | 0.931 | 0.915 |
| MemeTracker 短语 / clean | 1.118 | 1.294 | 0.929 | 0.855 |
| MemeTracker 短语 / dirty | 1.114 | 1.282 | 0.893 | 0.873 |

- 原BM路径对ART：URL两态数值仅多6.2%/6.9%，均在AA内，未分辨；这是达到必要数值预算，不能称等价或胜出。
  词表多约48%–50%、短语多约11%–12%，均形成清晰差距。
- 原BM路径相对完整目录在词表两态、URL clean及短语dirty存在清晰收益。
  不能把窗口纠正版没有净收益概括成“模型相对目录没有价值”。
- 自研F5在短语两态对ART少约14.5%/12.7%耗时，5/5更快并跨AA。
  URL两态数值较快但未跨AA；词表dirty约多9.7%且清晰更慢，clean未分辨。
  这是本轮F5控制的结果，不能当最新完整BlockLearn或旧Context16的结果。

## 工作账、空间与下一步

下面clean/dirty相同，所有65536条请求都调用原BM：

| 数据 | BM_base进入块 | 纠正后进入块 | 纠正目录比较 | BM自计字节 | F5自计字节 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 英文词表 | 737,807 | 65,536 | 280,911 | 1,944,569 | 1,997,593 |
| MemeTracker URL | 954,625 | 65,536 | 304,222 | 6,176,841 | 6,274,065 |
| MemeTracker 短语 | 725,742 | 65,536 | 281,924 | 7,039,184 | 7,135,696 |

这轮没有窗口外提议，但纠正仍每条承担窗口端点和目录搜索。访问块大降，完整耗时反增，
支持先减少额外fence工作、验证预测块优先策略；尚未量化某条指令或缓存占比。
空间为自计容量，排除节点链接、分配器元数据和scratch，不是完整堆/峰值或对ART的空间排名。

下一阶段先测最新完整BlockLearn原生工厂，再分开验证上游shortdir/first/cost，
与BM-page原路径和F5同输入比较；随后仅选一个有依据的接入候选。
短词差距是首要风险，URL原路径作为接近ART的保护格，短语用于验证长尾键处理。
动态吞吐矩阵继续后移，不用增加写入收益掩盖当前读回归。

## 证据与复现入口

- [源、采样、过滤、输入和复用二进制身份](results/bm_real_20261018/identity.json)。
- [原始3000行](results/bm_real_20261018/raw.json)、[预登记配对统计](results/bm_real_20261018/summary.json)、[判定](results/bm_real_20261018/decision.json)。
- [逐语料动态语义门](results/bm_real_20261018/corpus_gates.json)、[六格全部实现预检](results/bm_real_20261018/gates.json)。
- [独立复核](results/bm_real_20261018/independent_verification.json)：原TLI键到SIB输入无损、全部源码/数据/二进制哈希、
  3000行顺序/checksum/AA及所有预登记判定重新计算一致。
- 准备失败：[第一次下载限时](results/bm_real_preparation_20261018/attempt1.json)，native下载与最终发布记录归档在本轮frozen_sources中。
- 执行脚本：[run_bm_real.py](diagnostics/run_bm_real.py)，复核：[verify_bm_real.py](diagnostics/verify_bm_real.py)，
  补充直接对照：[analyze_bm_real_controls.py](diagnostics/analyze_bm_real_controls.py)。

生产默认不变；本轮完成真实键泛化描述，没有新增默认采用、冷读、混合写吞吐或尾延迟结论。

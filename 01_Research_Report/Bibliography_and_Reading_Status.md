# 文献清单与阅读状态

从完整研究报告第 3 节提取；原有 24 项工作、入口、优先级及阅读状态保持不变。此处未新增检索或扩大精读范围。

## 3. 完整文献清单与阅读优先级

P0：实施前必须掌握；P1：用于理解机制或设计实验；P2：专题补充。优先级是对你的工作而言，不是论文质量排名。文档要求近五年的相关研究，但它自己列了多篇经典论文，因此采用“经典必读＋2021–2026 年相关补充”的组织方式，而非严格按发表日截断。

### 3.1 项目文档列出的六篇

| 编号 | 论文与发表信息 | 原文 | 代码入口 | 主要借鉴 | 优先级 |
|---|---|---|---|---|---|
| R01 | The Adaptive Radix Tree: ARTful Indexing for Main-Memory Databases，ICDE 2013 | [作者 PDF](https://db.in.tum.de/~leis/papers/ART.pdf) | [ARTSynchronized](https://github.com/flode/ARTSynchronized)，后续实现 | 自适应节点、路径压缩 | P0 |
| R02 | The Case for Learned Index Structures，SIGMOD 2018 | [arXiv](https://arxiv.org/abs/1712.01208) | 本轮未核实与原论文完全对应的代码版本；可查看 [TLI](https://github.com/curtis-sun/TLI) 的 RMI 实现 | 分层模型、位置预测与纠错 | P1 |
| R03 | HOT: A Height Optimized Trie Index for Main-Memory Database Systems，SIGMOD 2018 | [论文 PDF](https://15721.courses.cs.cmu.edu/spring2019/papers/08-oltpindexes2/p521-binna.pdf) | [作者仓库](https://github.com/speedskater/hot) | 区分位、低树高、SIMD | P0/P1 |
| R04 | ALEX: An Updatable Adaptive Learned Index，SIGMOD 2020 | [arXiv](https://arxiv.org/abs/1905.08898) | [Microsoft ALEX](https://github.com/microsoft/ALEX) | 间隙数组、局部调整、成本模型 | P0 |
| R05 | The PGM-index: a fully-dynamic compressed learned index with provable worst-case bounds，PVLDB 13(8), 2020 | [正式 PDF](https://www.vldb.org/pvldb/vol13/p1162-ferragina.pdf)；[arXiv，题名有差异](https://arxiv.org/abs/1910.06169) | [作者仓库](https://github.com/gvinciguerra/PGM-index) | 误差约束、分段拟合、动态合并 | P1 |
| R06 | LITS: An Optimized Learned Index for Strings，PVLDB 17(11), 2024 | [作者正式 PDF](https://www.shimin-chen.com/papers/LITS-pvldb24.pdf)；[扩展稿](https://arxiv.org/abs/2407.11556) | [作者仓库](https://github.com/schencoding/lits) | 字符串模型与索引结构协同设计 | P0，最直接 |

### 3.2 为本任务补充的十二项

| 编号 | 论文／工作与发表信息 | 原文 | 代码状态或入口 | 借鉴重点 | 优先级 |
|---|---|---|---|---|---|
| R07 | Updatable Learned Index with Precise Positions（LIPP），PVLDB 14(8), 2021 | [PDF](https://vldb.org/pvldb/vol14/p1276-wu.pdf)；[arXiv](https://arxiv.org/abs/2104.05520) | [作者代码](https://github.com/Jiacheng-WU/lipp) | 用子节点解决预测冲突 | P0 |
| R08 | SIndex: A Scalable Learned Index for String Keys，APSys 2020 | [作者 PDF](https://ipads.se.sjtu.edu.cn/_media/publications/apsys20-final71.pdf) | [作者链接](https://ipads.se.sjtu.edu.cn:1312/opensource/xindex/-/tree/sindex)；本轮查看了 [TLI 收录代码](https://github.com/curtis-sun/TLI/tree/main/competitors/sindex) 的包装接口 | 公共前缀、partial key、增量区 | P1 |
| R09 | Bounding the Last Mile: Efficient Learned String Indexing（RSS），AIDB 2021 workshop / arXiv | [arXiv](https://arxiv.org/abs/2111.14905) | 本轮未核实独立官方仓库，不填猜测地址 | 限制最终纠错中的字符串比较 | P1 |
| R10 | Order-Preserving Key Compression for In-Memory Search Trees（HOPE），SIGMOD 2020 | [arXiv](https://arxiv.org/abs/2003.02391) | [作者代码](https://github.com/efficient/HOPE) | 保序压缩及编码成本 | P1 |
| R11 | Are Updatable Learned Indexes Ready?，PVLDB 15(11), 2022 | [PDF](https://www.vldb.org/pvldb/vol15/p3004-wongkham.pdf) | [GRE](https://github.com/gre4index/GRE) | 更新负载、分布变化、鲁棒性 | P0，实验部分 |
| R12 | Learned Index: A Comprehensive Experimental Evaluation，PVLDB 16(8), 2023 | [作者 PDF](https://dbgroup.cs.tsinghua.edu.cn/ligl/papers/experiment-learned-index.pdf) | [TLI](https://github.com/curtis-sun/TLI) | 统一比较、字符串评估、微架构指标 | P0，实验部分 |
| R13 | DILI: A Distribution-Driven Learned Index，PVLDB 16(9), 2023 | [arXiv](https://arxiv.org/abs/2304.08817) | [作者代码](https://github.com/pfl-cs/DILI) | 分布驱动的树结构与数据布局 | P1 |
| R14 | SALI: A Scalable Adaptive Learned Index Framework Based on Probability Models，PACMMOD 1(4), 2023 / SIGMOD 2024 | [arXiv](https://arxiv.org/abs/2308.15012)；[正式记录](https://dl.acm.org/doi/10.1145/3626752) | [作者代码](https://github.com/YunWorkshop/SALI) | 低成本统计、节点演化、并发扩展 | P1，后期 |
| R15 | Cutting Learned Index into Pieces: An In-depth Inquiry into Updatable Learned Indexes，ICDE 2023 | [官方记录](https://ieeexplore.ieee.org/document/10184769/) | 本轮未核实独立代码；仅完成书目与摘要级筛选 | 按组件分析，而非只比较整机吞吐 | P1，待补读 |
| R16 | LINE: A Learned Index with Group-Enhanced Leaves and Cache-Optimized Inner Tree，PACMMOD 4(3), SIGMOD 2026 | [作者 PDF](https://www.shimin-chen.com/papers/LINE-pacmmod26.pdf) | [作者代码](https://github.com/schencoding/line) | 分组叶节点、缓存、迁移与尾延迟 | P0/P1 |
| R17 | Understanding Robustness Issues of Updatable Learned Indexes: [Experiments & Analysis]，PACMMOD 3(4), 2025；作者文件标作 SIGMOD 2026 | [作者 PDF](https://minhui-xie.github.io/papers/sigmod26-robust_learned_index.pdf) | 论文指向 [RoBin](https://github.com/cds-ruc/RoBin)；本轮页面未成功打开 | 极端插入、空间膨胀、最差情况 | P1 |
| R18 | LITS 扩展代码 LIT+ / LIT-MT；另检索到 Learned Indices for String Keys with Optimized Performance and O(N) Space Cost 的 TODS 在线记录，2026-08-17 | [TODS 记录](https://dl.acm.org/doi/10.1145/3841175)；[扩展源码说明](https://github.com/schencoding/lits/tree/master/LITS%2B) | [LITS 作者仓库](https://github.com/schencoding/lits) | 检查已有优化，避免重复设计 | P0，确定创新前 |

关于 R18：本轮未取得该 TODS 全文。仓库总 README 仍写“TODS 2025 / Paper: To be announced”，与检索到的在线出版日期不一致；不要据此将两个版本的全部技术细节视为已逐一对应。下面涉及 LIT+ 的判断以实际读取的源码和说明为依据。

R08、R09 是 workshop 工作，R18 是期刊补充，均应单独标注，不能算作 SIGMOD/VLDB/ICDE 正式主会论文。它们仍与字符串任务高度相关。

### 3.3 课件参考页另外列出的两篇

| 编号 | 论文 | 原文 | 如何使用 |
|---|---|---|---|
| R19 | Edward A. Fox, Qi Fan Chen, Lenwood S. Heath：A Faster Algorithm for Constructing Minimal Perfect Hash Functions，SIGIR 1992 | [论文 PDF 镜像](https://sources.debian.org/src/cmph/2.0.2-3.1/papers/fch92.pdf) | 静态集合完美哈希的背景；不是本项目动态有序索引的主线 |
| R20 | Nima Shahbazi, Stavros Sintos, Abolfazl Asudeh：FairHash: A Fair and Memory/Time-efficient Hashmap，PACMMOD 2(3), SIGMOD 2024 | [正式记录](https://dl.acm.org/doi/10.1145/3654939)；[arXiv](https://arxiv.org/abs/2307.11355) | 数据相关哈希与群体公平性背景；当前研究相关度较低 |

这两项完成了原文入口检索，当前不建议投入与 LITS 相同的精读时间。SwissTable 可直接读上文链接的 Abseil 官方设计说明。

### 3.4 推敲实现后补充的直接对照

| 编号 | 工作 | 原文／官方入口 | 与候选方案的关系 | 阅读状态 |
|---|---|---|---|---|
| R21 | CARMI: A Cache-Aware Learned Index with a Cost-based Construction Algorithm，PVLDB 15(11), 2022 | [正式论文](https://www.vldb.org/pvldb/vol15/p2679-gao.pdf)、[扩展稿](https://arxiv.org/abs/2103.00858)、[作者代码](https://github.com/embryo-labs/CARMI) | 已有预测目标数据块、配合预测安排存储和提前预取，是本课题第二项创新必须对照的方法；原文实验使用数值键 | 阅读 §5.3 及扩展稿相关设计，检查预取模型代码入口 |
| R22 | Learned Indexes for a Google-scale Disk-based Database，NeurIPS ML for Systems Workshop 2020 | [论文](https://mlforsystems.org/assets/papers/neurips2020/learned_abu-libdeh_2020.pdf) | 已有单调字符串模型、累计字节位置预测与按模型输出分块；场景为 Bigtable/SSTable，不是本项目的动态内存索引 | 阅读 §4；不把“预测块而非记录”当作本课题独立新贡献 |
| R23 | LIVAK: A High-Performance In-Memory Learned Index for Variable-Length Keys，DAC 2024 | [会议官方介绍](https://61dac.conference-program.com/presentation/?id=RESEARCH954&sess=sess117)、[ACM](https://dl.acm.org/doi/10.1145/3649329.3657385) | Trie、学习索引与 B+ 树混合，每层处理 8 字节键片段；应比较字符串分层方式 | 官方摘要级核对，未取得全文或核实作者代码 |
| R24 | LISK: A High-Performance In-Memory Learned Index for Variable-Length String Keys，IEEE Transactions on Computers，2026 | [出版方入口](https://www.computer.org/csdl/journal/tc/5555/01/11458592/2fj9JvQyrqU) | 新近的变长字符串学习索引；分片与两层学习子索引是进一步查新的重点 | 出版方检索信息级核对，全文及具体机制尚待补读；不据此断言与本方案相同或不同 |

R21 为 P0 方法对照；R22 为 P1 机制对照，属于经典系统工作补充；R23/R24 为实施前应补读的字符串近邻。其发表场合与阅读状态如上，不能统一称作“近五年三大数据库会议且均已精读”的文献。


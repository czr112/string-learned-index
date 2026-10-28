# 原生 BlockLearn cost/adaptive 独立消融登记

2026-10-19，承接长期计划阶段1§4、阶段3§4；在首次校准、运行之前冻结。本轮是**完整上游工厂参照**，
不改物理页、不组合已关闭的簇细分，不称为 BM-page 的新采用候选。

工厂按原参数：`block-residual-delta-shortdir-first`（legacy）、同名`-cost`、
`block-adaptive-delta-shortdir-first`（adaptive）。加同轮 ART 与冻结F5，各自同对象AA，共10臂。
不跑12工厂笛卡尔积，HOT原失败保留、不排名。

原生按record建模，校准和 planner 的16项 tag batch 是其真实工作单位；不把该单位
套到本项目按fence建模的物理页。接入BM-page成本目标需另做单位转换及验收。

## 先校准与正确性

原样编译上游 calibrate_planner，O3/NDEBUG/x86-64-v3，与已冻结原生Release库相同target/build/ISA。
CPU0，独立合成数据、固定上游seed、32,768 iterations；只生成一个profile，不按测试结果重校准。
生成时不传数据集/查询；校准后验证target=x86_64-avx2、build=release与所有正系数，固定文件哈希。
sanitizer二进制仍定义NDEBUG，以验证同Release profile，仅做正确性不出性能排名。

三工厂先走同一个公共接口验收，涵盖声明能力、空/NUL/高位字节/长键/0/MAX/owning scan；
再对四固定语料（words/URLs/phrases/tranco64，各65,536）全行、全部约90%命中查询、全scan、
scan下界和validate逐条检查。普通及ASan/UBSan/LSan全部通过，才启动任何正式计时。
计数用另建instrument=1对象，报告tag批次、完整比较、叶数、hash/residual/first选择；
这些计数和profile计时不混入吞吐。父目录比较与完整键验证在原生计数中不能拆开的部分明确披露。

## 正式计时与判读

所有实现同一新benchmark contract、同一完整find虚调用、同一输入/每轮排列；
5独立进程×10轮×10臂，10臂Williams顺序轮换，每轮含全部臂和同对象AA；CPU0，O3，无LTO。
建索引及正确性预检在query计时外；构建耗时逐对象单列。查询与索引寿命满足借用输入contract。
每进程先算10个直接配对比值的轮内中位数，再对5进程取中位数；不乘历史倍率、不挑最快轮次。
AA带取本格各同对象AA所有进程偏离1的最大值，属观测噪声带，非置信区间或等价性证明。

主要比较cost/legacy、adaptive/cost，辅助两新工厂/ART、legacy/ART、F5/ART、构建cost/legacy及adaptive/legacy。
清晰差异需至少5%、4/5同向且超AA；可复用收益需至少10%、4/5同向且超AA，
无其它真实格清晰>5%回归。通用静态目标仍需三真实格均≤1.10 ART（tranco另列风险），不改门。
构建≤1.20、总容量≤1.10 legacy另列；原生接口不拆纯模型元数据，因此**5%元数据预算仍未知**。
容量包括index_owned与retained input；是估计，不冒充完整堆/RSS/峰值。正确性通过不能替代采用条件。

本轮无动态吞吐、无新样本泛化、无默认采用；若两新工厂仍无跨真实数据收益，
停止叠查询机制，按计划转完整空间/维护路线。所有失败目录保留，不为挽救性能追加重复。
源码、原生库、ART库、数据、校准和二进制运行前后固定哈希并归档复核；规格结果另写报告。

## 前置修复登记（尚无计时数据）

首次运行的公共验收将 `rows_of(...)` 临时vector传给允许借用输入的原生工厂，调用后立即销毁，
违反contract第83–84行的输入存活条件，find随后访问失效vector；旧输出中的0xff又触发日志UTF-8解码失败。
旧源码、校准、二进制、原始输出和ASan诊断保留在首次目录。新独立公共验收副本仅将所有bulk_load
实参存到测试拥有的稳定deque，直到全部索引销毁后再释放；原断言逐字不变，原公共文件不改。
这只修复调用方生命周期，不包装/复制计时臂的输入，不改变模型所有权或能力。
日志改为捕获原始bytes，失败时存原始输出并用转义文本展示。复用第一次唯一校准及未变的
calibrator/benchmark二进制；只重编译修正后的普通/sanitizer验收程序。判据与计时次数均不变。

# 最新上游的 BM 接入与首次优化：预登记

本轮在任何成功计时之前固定 benchmark `003a55bf674b43350fbb70242070fb74f33a56ce`
及模型 `e8e05411135b222edb82e3861799a2ae07ae5470`，由独立 snapshot 编译。
原 `60837a8` 语义结果、原仓库检出及旧协议不修改。全部翻译单元使用相同的新 contract，
`concurrent_find` 保持 false，本轮单线程。

接入沿用 BM_PAGE_SPEC 的物理 fence 训练及稳定字节所有权。实际编译未经修改的
上游 block_model.cpp；fit 固定 1/7/8/1，保持同一 BM 页上的 raw-window 与窗口内精确
纠正对照。本轮未打开新增的短目录、predicted-first 或自动 cost planner，它们各需独立消融。
新 `window` 内部调用上游 `locate`，本轮仍由完整 fence 比较保证任意缺席键正确。

普通及 ASan/UBSan 完整语义重验后，按 BM_READ_SPEC 的行、查询、clean/dirty、
1024 次既有值 update、查询缺席构造、warmup、checksum 和判定门测量。
原 12 臂预检中 HOT 共用验收发生 LSan 96 字节泄漏，未开始计时；失败见
results/bm_page_read_20261018/failure.json。本轮预先排除 HOT，保留 BM_base、BM、
directory、F5、ART 及各自同实例 AA。ART 全源码普通/完整 sanitizer 重建并验收。

固定 5 进程 × 10 轮 × 10 臂 × 6 格 = 3000 行。Williams 顺序
[0,1,9,2,8,3,7,4,6,5]，按 repeat+process 旋转，单 CPU0，O3、x86-64-v3、无 LTO。
先配对每进程 10 轮中位数，再取 5 进程中位数；沿用双方 AA 最大观测带。
mixed/skew 两态全部 BM/BM_base<=0.90、至少 4/5 更快且超过双方 AA 才通过优化收益门；
tranco 两态不得出现 >5%、至少 4/5 更慢且超过 AA 的回归。不以追加重复挽救失败。
直接报告 BM/目录、自研 F5、ART；BM/ART 每格<=1.10 仅是通用读必要目标。

自计空间不是完整分配峰值；现有 update 后读不是混合读写吞吐。未通过的策略不采用，
BM 模型仍是研究主线，生产默认不变。新的混合操作 runner 留给后续动态矩阵直接复用。

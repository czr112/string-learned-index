# benchmark 真实数据补测：固定协议

目的：独立真实键形态验证，不修改或挽救 BM_LATEST 首轮整体失败判定。
本轮只变更数据，模型、物理页、参数、查值路径和判据保持固定。
benchmark 003a55bf、模型 e8e05411；远端已再次核对，仍为这两个提交。

数据由该 benchmark 原 `scripts/prepare_real_datasets.py` 按其 datasets.lock.json 下载并核对
完整大小/SHA-256，完整扫描至 EOF，按 bottom-k BLAKE2b 哈希选唯一键。
英文词表、MemeTracker URL、MemeTracker 短语，各 65,536 键，seed=42。
首次WSL下载触发原300秒限时，英文词表样本保留，MemeTracker部分文件由原脚本清除；
失败见results/bm_real_preparation_20261018/attempt1.json。随后用native Python按相同URL、
字节数和SHA-256下载，单独900秒预算，再交给未修改的上游脚本离线提取MemeTracker两类键。
native完整文件校验通过后hardlink发布被本机拒绝，完整内容保留；二次核对哈希后用同目录rename发布，
原失败及最终publication记录均归档，没有重下、修改文件内容或覆盖已有raw文件。
没有更换数据或省略全文件扫描。下载原文与样本均保留在忽略的snapshot/data目录中。
保留原字节、排序去重；按原脚本记录空键/NUL/>4096B过滤及键长/LCP分布。
这是 phrase-cluster 来源，不是 TLI 90M URL 或真实查询日志，不外推百万级结果。

复用已验收且逐字节哈希一致的 BM core 检查和 BM/ART 计时二进制，全部实际源码归档，
避免无改动重复编译。原 common99与完整二进制 instrumented 身份绑定保留。
每份新数据先完成普通/ASan corpus 的全量查询、增删改、scan、compact 对照；
再完成 clean/dirty 六格普通与 sanitizer 的全部实现逐键/查询/scan 检查，全部通过才计时。
dirty 为原 seed=0xf2a20261018 的1024次已有键值更新，根发布数保持1。

源键全量一次，Python Random(20261018) shuffle，枚举值保持排序序号，原驱动每十条追加
~f2/miss 并证明缺席，约90%命中。由 benchmark 原 legacy-tli reader 和 interchange writer 转换，
不丢弃空格、标点、高位字节或超长尾部；所有实现使用同一 rows/queries 文件。

BM_base、BM、directory共用同一个物理索引；自研控制F5、ART使用同一map；各自同实例AA。
HOT固定适配器仍有上一轮LSan泄漏，当前不纳入，不改其代码或关闭leak检测。
固定5进程×10轮×10臂×6格=3000行；Williams顺序[0,1,9,2,8,3,7,4,6,5]，
repeat+process旋转；每轮同一查询顺序及checksum，单CPU0、原O3/x86-64-v3、无LTO。

沿用每进程配对比值中位数，再取5进程中位数；双方同实例AA取最大观测带。
差异至少5%、至少4/5进程同向、且超双方AA才称清晰。每格BM/ART<=1.10为必要数值目标，
不以平均掩盖失败。新数据的局部收益仅描述观测，不撤销旧mixed/tranco的失败，不自动采用。
不调参数、不追加性能重复、不据此宣称混合读写吞吐、冷读、尾延迟或完整堆空间排名。

import os
P = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md'
s = open(P, encoding='utf-8', newline='').read()
anchor = '- **下一步三选一**（一轮只做一项）：'
if s.count(anchor) != 1:
    raise SystemExit('anchor %d' % s.count(anchor))
note = '''- **方向①已执行完实现轮（2026-10-11）**：[RESULTS_DEPTH_DIR_20261011.md](RESULTS_DEPTH_DIR_20261011.md)。
  第四个后端 `ModelBackend::EqualDepthDir`（`model/equal_depth_dir.hpp`，默认关闭）＋ 门
  `check_depth_dir`（三语料 131,626 项检查 0 失败，含独立重算、精确等深模式 12/13、重复键与域套件、
  旋转负对照）＋ sanitizer 三格全绿 ＋ A/B `bench_depth_dir`（5 进程 × 4 重复、重复内配对、20 对/语料、
  0 完整性问题）。**判定分裂**：skew 净收益（静态 0.903、更新后 0.779，5/5），mixed 静态回归
  （1.142，0/5，噪声底 1.128），tranco 未分辨（1.257，噪声底 1.321）；构建与维护三语料全快
  （0.87–0.95 / 0.43–0.71，5/5）。常驻 mixed +2.21 MB（≈+5.6%，超 ≤5% 预算）。按计划 §6 采用规则
  （主目标 mixed ≤0.90）**不设默认**，保留为默认关闭候选＋场景结论（skew 可用）。
  机制面：主块访问降到 ≈1.00 次/查询、溢出归零；机制假设（未验）为未截断 arena 的缓存不友好，
  下一轮若继续，预登记只做「截断分隔符」且必须先解决截断下的正确比较语义。
  身份：index.hpp `8688225e…`、switches.hpp `12a497a2…`、model/equal_depth_dir.hpp `07495f52…`，
  四个适配包 manifest 与 `diagnostics/run_owner_lifecycle.py` 已重钉；既有五个门/基准重建全绿。
'''
s = s.replace(anchor, note + anchor)
open(P, 'w', encoding='utf-8', newline='').write(s)
print('state doc updated, bytes', os.path.getsize(P))

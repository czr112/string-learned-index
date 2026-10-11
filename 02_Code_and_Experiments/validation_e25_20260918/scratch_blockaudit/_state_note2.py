import os
P = r'D:\StringLearnedIndex_Workspace\07_Eval_Integration\STRUCTURE_OPTIMIZATION_STATE.md'
s = open(P, encoding='utf-8', newline='').read()
anchor = '- **下一步三选一**（一轮只做一项）：'
if s.count(anchor) != 1:
    raise SystemExit('anchor %d' % s.count(anchor))
note = '''- **方向①的入口诊断已完成（2026-10-11）**：[RESULTS_THRESHOLD_ENTRY_20261011.md](RESULTS_THRESHOLD_ENTRY_20261011.md)。
  等深阈值目录的**分隔符形式**第一次真正消除组负载集中：最大组负载 1366/824/546 → 12/12/12、空组
  14278/19178/5993 → 0/0/0，压缩分隔符常驻 0.6–5.0 KB/区域（被替换的 Context16 参考表是
  16,512 B/区域）；查找 7.5–8.3 步/查询、比较 21–127 B/查询（按 memcmp 首个不同字节计费）。
  **63-bit 坐标形式不成立**（skew 64/64 区域阈值重复，最大组负载 1431 > Context16 的 824）。
  子块数没有改善（+13%/+35%/+1%），所以收益的机制是「把模型预测 + 组内目录两次搜索合成一次等深二分」，
  不是「更少的子块」——这一条写进了结果文档的边界段。下一轮预登记为「最小差分实现」：
  第四个 ModelBackend（默认关闭）+ 正确性门 + 同状态入口诊断，未通过不进 A/B。
'''
s = s.replace(anchor, note + anchor)
open(P, 'w', encoding='utf-8', newline='').write(s)
print('state doc updated, bytes', os.path.getsize(P))

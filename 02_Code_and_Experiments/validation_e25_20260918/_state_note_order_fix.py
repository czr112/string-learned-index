import io
p = r'07_Eval_Integration/STRUCTURE_OPTIMIZATION_STATE.md'
s = io.open(p, encoding='utf-8', newline='').read()
old = '本次只完成核查与计划，生产源码未改、后续性能实验未启动。'
new = ('本次完成核查与计划；**P0 已执行收口（2026-10-11 晚）**：目录搜索改为显式无符号字节比较，'
       '新门 `check_depth_dir_order`（含 sanitizer 孪生）与两个既有目录门的反例检查全部通过，'
       '见 [RESULTS_DEPTH_DIR_ORDER_FIX_20261011.md](RESULTS_DEPTH_DIR_ORDER_FIX_20261011.md)。'
       '下一步为 P1 读路径入口诊断（不接计时）。')
if s.count(old) != 1:
    raise SystemExit('anchor not found: %d' % s.count(old))
io.open(p, 'w', encoding='utf-8', newline='\n').write(s.replace(old, new))
print('state doc updated')

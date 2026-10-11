import hashlib, io
V = r'02_Code_and_Experiments/validation_e25_20260918/'
D = r'07_Eval_Integration/RESULTS_DEPTH_DIR_ORDER_FIX_20261011.md'
gate = hashlib.sha256(open(V + 'check_depth_dir_order.cpp', 'rb').read()).hexdigest()
script = hashlib.sha256(open(V + 'run_order_fix_longkeys.sh', 'rb').read()).hexdigest()

s = io.open(D, encoding='utf-8', newline='').read()
reps = [
 ('4. **完整 Index**：`region_keys=64` 多区域；空键、NUL、`7f/80/ff`、共享前缀族；',
  '4. **完整 Index**：`region_keys=64` 多区域；空键、NUL、`7f/80/ff`、共享前缀族；'
  '两个 4096 字节长键（其一以 `0xff` 结尾，均超出子块容量，走溢出放置、scan 物化与擦除）；'),
 ('| `validation_e25_20260918/check_depth_dir_order.cpp`（新门） | `bfb6214f7d7e8e6710697f2467ad627ccfd1eca53df403937865ddfa0a63bd8c` |',
  '| `validation_e25_20260918/check_depth_dir_order.cpp`（新门） | `%s` |' % gate),
 ('| `validation_e25_20260918/run_order_fix_inherited.sh` | `d145e98a3c897c3bc9e044d752b6926eb13e8de081843f0088c7789702934d83` |',
  '| `validation_e25_20260918/run_order_fix_inherited.sh` | `d145e98a3c897c3bc9e044d752b6926eb13e8de081843f0088c7789702934d83` |\n'
  '| `validation_e25_20260918/run_order_fix_longkeys.sh` | `%s` |' % script),
 ('bash run_order_fix_inherited.sh   # 重建并重跑受影响的契约/扫描/路由/空洞门',
  'bash run_order_fix_inherited.sh   # 重建并重跑受影响的契约/扫描/路由/空洞门\n'
  'bash run_order_fix_longkeys.sh    # 长键加入 Index fixture 后的三门重跑（含 sanitizer）'),
 ('（“检查数 / 失败数”；日志 `logs/order_fix_gate_20261011.log`，全部 rc=0。）',
  '（“检查数 / 失败数”；日志 `logs/order_fix_gate_20261011.log`，全部 rc=0。）\n'
  'Index fixture 加入两个 4096 字节长键后，本门与 sanitizer 孪生在三语料上重跑同样全绿：\n'
  '`logs/order_fix_longkeys_20261011.log`。'),
]
for old, new in reps:
    if s.count(old) != 1:
        raise SystemExit('anchor not found (%d): %s' % (s.count(old), old[:60]))
    s = s.replace(old, new)
io.open(D, 'w', encoding='utf-8', newline='\n').write(s)
print('results doc updated; gate', gate[:12], 'script', script[:12])

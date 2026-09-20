# 原生基线接入与检查状态

正式性能对照只包含已过本轮门槛的 **armon/libart**，不包含 LITS。本文件记录的是所测提交与输入的表现，不能推广为所有版本、所有负载的判断。

| 版本／检查 | 真实结果 | 日志 |
|---|---|---|
| 原生 libart，发布构建 | 10000 次随机操作；33330 次精确核对通过 | `results/checks/art_release.log` |
| 原生 libart，ASan+UBSan | 同样检查通过 | `results/checks/art_sanitizer.log` |
| 无修改 LITS，发布构建 | 10000 次随机操作；33330 次精确核对通过 | `results/checks/lits_release.log` |
| 无修改 LITS，ASan+UBSan，-O1 | PMSS 读 index 18，合法列是 0..17 | `results/checks/lits_sanitizer.log` |
| LITS，仅 PMSS 补丁，ASan+UBSan，-O1 | HPT::getCdf 在 gcpl=0 时读 key[-1]，堆前越界 | `results/checks/lits_pmss_patch_sanitizer.log` |
| LITS，PMSS+HPT 补丁，ASan+UBSan，-O0 | 内嵌 HOT 读取未按 8 字节对齐的 uint64_t | `results/checks/lits_bounds_patch_sanitizer.log` |

第一项修复将 PMSS 双线性插值的 upper_bound 限制在最后一行／列；到达上界时插值权重已是零，但 C++ 表达式仍会求值并读数组，因此原代码依旧越界。第二项修复在 HPT::getCdf 与 getPos 起始位置使用上下文 0，与其 train 中 b==0 的规则一致。完整补丁程序是 `apply_lits_patch.py`，原始与修复文件摘要见 `sources.json`。

第三项在 x86 上可能仍能运行，但 UBSan 检出了 C++ 对齐约束问题。本轮没有继续改写 HOT 字节提取路径，也没有屏蔽 alignment 来声称通过。发布构建返回正确值不等于无未定义行为；本轮不报告 LITS 与原型之间的正式速度结论。

依赖源码采用固定提交，初次复现调用 `fetch_sources.py`。之后可用下列命令重现诊断（建议在单独复制目录中操作，编译资源消耗较高）：

```bash
python validation_e4_20260911/fetch_sources.py
# 此时是原始版本，直接编译检查可复现第一处问题。
python validation_e4_20260911/apply_lits_patch.py
# 此时是双补丁版本，以下命令重现最后一处门槛失败。
gcc -std=c99 -O1 -g -march=native -fsanitize=address,undefined -fno-omit-frame-pointer -c validation_e4_20260911/vendor/libart/src/art.c -o validation_e4_20260911/build/art_san.o
g++ -std=c++17 -O0 -g -march=native -w -fsanitize=address,undefined -fno-omit-frame-pointer validation_e4_20260911/check_native.cpp validation_e4_20260911/build/art_san.o -o validation_e4_20260911/build/check_native_san_bounds
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 validation_e4_20260911/build/check_native_san_bounds lits
```

LeakSanitizer 退出扫描因运行环境的进程可见性限制关闭；ASan 的越界检测与 UBSan 仍开启。未完成内存泄漏专项结论。

来源：[LITS 固定提交](https://github.com/schencoding/lits/tree/6f4793dff0dcf66daada49e2f4ae1b1838bfa9cc/LITS)、[libart 固定提交](https://github.com/armon/libart/tree/301046804af165269e37da6725f5a4aec9ecc881)。libart 是这个具体的 C99 ART 实现，不是 ART-OLC，也不代表 ART 类结构的最优性能。

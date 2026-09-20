# E5 实验改造日志（validation_e5_20260913）

开始日期：2026-09-13。本目录继承 `validation_e3_20260906` 的存储实现（index.hpp / selected.hpp / data.hpp 起始副本），在本文件中**逐条记录全部代码改进**：每项改动写明动机、改了什么、如何验证。原始三轮目录保持字节不动。

环境差异声明：本轮在 Windows 11（10.0.26200）＋ MinGW-w64 g++ 15.2.0 ＋ Intel Core Ultra 9 275HX（24 核，16 GB）上执行；原三轮为 Linux x86-64 ＋ g++ 13.3 ＋ taskset 绑核。本轮用 PowerShell `ProcessorAffinity` 绑定单核替代 taskset。**本轮所有时间数字只做本轮内部比较，不与原三轮绝对值拼接加速比。**

## 改造记录

### E5-0 基线确认（2026-09-13）
- E3 原码（check.cpp）在本机 g++ 15.2.0 `-std=c++17 -O2 -march=native` 编译通过，未修改。
- Tranco XN6XN 快照下载并解包（ZIP 内 top-1m.csv），SHA-256 与 `tranco_source.json` 记录一致（8ab2bf…42e），1,000,000 行。存放于本目录 `data/tranco_XN6XN.csv`，不打包分发。

### E5-1 可变条目槽数 S（2026-09-13 完成）
动机：E3 容量统计证明同一溢出率对应不同资源瓶颈（Tranco 574,477 条溢出全部为"仅槽不足"，skew 87.65% 为"仅字节不足"），固定 16 槽无法响应两类瓶颈。
改动点（对应《三轮验证代码结构讲解》第 8 节接线清单）：
1. `Config` 增加 `slots`（默认 16，保持旧行为可复现）。
2. `Header::tags[16]` → `tags[48]`：8+8+48=64B 不变，`static_assert(sizeof(Header)==64)` 保留；S≤48 时 Header 尺寸恒定，tags 起始偏移不变（偏移 16，16 字节对齐），S>48 在构造时拒绝。
3. `Region` 的 `K`/`START` 常量改为成员 `slots_`/`start_=sizeof(Header)+S×16`，构造函数校验 `B>start_` 且 B 为 64 倍数。
4. `put_primary`/`compact`/`find_impl`/`scan`/`group_stats`/构造函数全部改用 `slots_`/`start_`，组数估算 `键数/(S×load)`。
5. `check.cpp` 容量守恒从硬编码 `block_bytes-320`、`==16` 改为按 `64+S×16` 与 S 计算。
6. 候选编码改为显式 `cand_table[25]`（`{model,block,slots}`）：id 0–8 保持 E3 含义（3 模型×3 块宽，S=16）；9–17 增加 S=8；18–23 增加 S=32（b512 因 START=576 非法而排除）；id 24 = 有序叶回退；`SORTED_LEAF=24`、`CANDIDATES=25`。

### 事故记录：源码覆盖与重建（2026-09-13 11:09）
- 会话恢复阶段误将 E3 原始 `index.hpp / selected.hpp / data.hpp` 用 `cp` 覆盖到本目录，E5-1 已改造的 index.hpp 被回退为原版。幸存：`check.cpp`（01:18 按改造后 API 编写的重写版）、`build/` 旧产物、`LOG.md`、`environment.json`、`data/tranco_XN6XN.csv`。
- 处置：以幸存 check.cpp 的调用规格＋本日志 E5-1 设计为准重建 `index.hpp`；`selected.hpp` 重建 `cand_table/choice`、更新 `SORTED_LEAF/CANDIDATES`、补 `SelectedLeaf/SelectedIndex` 的 Mode 感知 `find` 重载；`data.hpp` 复核确认 E3 版未被改造过（字节数一致），无损失。
- 教训（流程修正）：任何覆盖写之前必须先核对目标目录现状；本条目即按该教训补记。

### E5-1b 编译参数修正：`-march=native` → `-march=x86-64-v2`（2026-09-13）
现象（最小复现 t2，同一二进制多次运行，崩溃为间歇性）：
- `-O0 -march=native`：10/20 崩溃；`-O0`（基线 x86-64）：0/20；`-O0 -march=x86-64-v3`：10/20；`-O2 -march=native`：13/20；`-O2 -march=x86-64-v2`：0/20；`-O2 -march=native -mno-avx`：0/20；`-O3 -march=x86-64-v2`：0/40。
- 排除项：64 字节对齐 `operator new` 单独测试 5000 次无失败、对齐恒为 64；崩溃与索引逻辑无关（最小复现只含构造）。
- 结论：崩溃仅在 GCC 15.2.0 (MinGW-Builds) 生成 AVX(256 位) 指令时以约 50–65% 概率出现，怀疑为该工具链 × Core Ultra 9 275HX（P/E 混合核）× Windows 11 26200 的组合问题；本机 ASan 不可用（无 libasan），无法进一步定位根因。
- 决定：E5 全部编译改用 `-O3 -march=x86-64-v2`（本代码所需全部指令——CRC32 需要 SSE4.2、tag 筛选需要 SSE2——均在 v2 内），`environment.json` 同步更新。所有 E5 时间数字标注该参数；不与原三轮（native，Linux）跨环境比较。

### E5-1b 修正：根因是 libstdc++ DLL 错配，非 AVX（2026-09-13 补记）
- 上节的"AVX 触发"推断**不完整**。混合/重复前缀数据通过 `check` 与基准后，Tranco 路径以 100% 概率崩溃（与数据规模无关、确定性），`-fno-inline` 消除、`noinline`（仅 make_data）无效，说明与内联的 libstdc++ 头文件代码有关。
- gdb 回溯（在纯 ASCII 路径目录下进行，规避中文路径下断点绑定失败）抓到崩溃帧：`#0–#2 位于 C:\Program Files\Git\mingw64\bin\libstdc++-6.dll`，调用方为 `validation::make_data`。
- **根因**：Git Bash 的 PATH 使运行期 DLL 搜索先命中 Git 自带的 `libstdc++-6.dll`（版本与 MinGW-Builds g++ 15.2.0 不匹配）；tranco 分支大量使用 iostreams，稳定踩中错配路径。此前 t2 的间歇崩溃与"AVX 相关性"是同一错配在不同代码生成下的表象，x86-64-v2/`-mno-avx` 只是恰好避开了出错路径。
- **修复**：编译加 `-static`（静态链接匹配版本的 libstdc++/libgcc，运行期不再搜索 DLL）。验证：t3（tranco 加载＋建索引）5/5 通过；-march 参数恢复为与报告一致的 `-O3 -march=x86-64-v2` 并保留。
- 环境记录：environment.json 的 compile_flags 已加 `-static` 并注明。

### E5-2 SIMD 批量 tag 筛选（2026-09-13）
动机：第一轮起块内候选筛选一直是标量逐条比较（VALIDATION §2 明确列为未实现项）；S=16 时 16 个 tag 恰好一个 SSE2 比较＋movemask 完成。
改动：`Mode` 增加第 4 字段 `simd`（默认 false，保持旧行为）；`find_impl` 在 `Hash && __SSE2__ && mode.simd` 时按 16 字节块 `_mm_load_si128`＋`_mm_cmpeq_epi8`＋`_mm_movemask_epi8` 批量筛 tag，掩码按 `min(count,块末尾)` 截断（count 之外的陈旧 tag 字节永不产生候选），命中位按 `ctz` 迭代进入原有精确比较；无 SSE2 或 `simd=false` 走原标量路径。Header 内 tags 起始偏移 16、块 64 字节对齐 ⇒ 每块加载均为 16 字节对齐。
验证：check.cpp 每次查找同时以 scalar/simd 两种 Mode 调用并断言结果一致（5409 对，3 次运行全过）；tag 筛选仍只影响性能不影响正确性（精确比较未动）。

### E5-1/E5-2 验证记录（2026-09-13 11:2x）
`check.cpp` 在重建后的头文件上以 `-O3 -march=x86-64-v2` 编译，**3 次运行全部通过**：
`operations=51000`（std::map 差分，7 个 trial 覆盖 S=8/16/32 与异质混合、每 223 步随机 reconfigure）、`scans=7777`、`simd_scalar_find_pairs=5409`、`model_checks=5460`（1365 穷举短串×4 布局的组号一致＋单调）、`group_stats_checks=44035`（容量按 64+S×16 计算）、`fingerprint_checks=8716`、`rebuilds/splits=328/98`、`selection_checks=100`（最大差距 14.5 合成单位）。原始输出 `logs/check_run.log`。

### E5-3 基准程序 bench_e5.cpp（2026-09-13 12:0x）
新增 `bench_e5.cpp`（不属于 E3 复制件）：①8 个 S×B 布局×{SIMD,标量}两种 Mode（同一索引内配对比较）＋全部有序叶＋有序数组，context16 固定（本轮问题是布局不是模型）；②开销分解：locate_only／region-local（find_known）／end-to-end 在参考布局 16×1024 上单独计时；③查询为独立副本、50% 命中、`~missing~` 未命中计时外核实不存在、3 种子×5 轮、乱序＋预热＋批校验和（继承原纪律）；④进程内 `SetProcessAffinityMask(1)` 替代 taskset，替代 PowerShell 外部绑核方案。冒烟 16384 键通过后跑正式三组数据。
编译修复：`const auto q` 改 `auto q`（region 字段预计算需写入）、补 `<functional>` 与 volatile sink。

### E5-4 结果汇总（2026-09-13）
三组数据正式运行完成（sweep 270 行/组＋decomp 60 行/组，全批校验和通过）。要点：
1. 溢出总数对 S 几乎不敏感（tranco S=8→32 仅 159.6k→132.4k）：溢出由分组偏斜主导；S 只在"槽不足↔字节不足"之间搬运瓶颈（tranco S=32/B=1024 出现字节不足类别；mixed/skew 的 S=32/B=1024 溢出全部变为字节不足）。
2. 溢出扫描次数 ovchk 与端到端时间强相关（skew 最高 295.7 次/查询）——改善分组质量与溢出访问组织是比调 S 更大的杠杆。
3. 开销分解：外层路由单项 172–268ns，占端到端 15–26%——下一结构改动第一优先级。
4. SIMD tag 筛选：tranco S≥16 配对中位数 +2.3%～+5.0%，mixed/skew 无方向；与预取同判——保留开关，不作改进声明。
5. 端到端：学习布局在三组数据全部快于有序数组（−22%～−30%）；无通用最优布局，S 加入选型轴有直接证据。
详见 `RESULTS_E5.md`。

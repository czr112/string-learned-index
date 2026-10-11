# 本机执行环境：WSL 不可达，改用原生 Windows + MinGW-w64 构建（2026-10-10）

本轮（阶段 D）开始前，本机**无法使用 WSL**，而项目此前的 E25 及以后各轮均在
`WSL2 Ubuntu + g++ 13.3.0` 下构建。本文件只记录"为了让装置在本机跑起来"所做的事、
如何复现，以及由此产生的可比性边界。**它不是实验结果，也不改任何结论。**

## 1. 为什么不是 WSL

- `wsl.exe`（含 `-l -v`、`--status`、`bash -lc`）在本会话一律返回 `Wsl/E_ACCESSDENIED`；
  这是沙箱层面的拒绝，不是 WSL 未安装。
- 越出沙箱执行（`require_escalated`）是唯一通路，但自动审批在本会话**系统性失败**
  （reviewer 返回 `stream disconnected` / 无法解密，属审批链路故障，不是策略拒绝），
  因此 WSL、Docker Desktop（后端是 WSL2，且需要 GUI 启动）都不可达。
- 结论：本机可用的、无需提权的构建路径只有**原生 Windows 工具链**。

## 2. 实际使用的工具链

| 项 | 值 |
|---|---|
| 编译器 | `D:\STM32\mingw64\bin\g++.exe` — MinGW-w64, x86_64-w64-mingw32, **g++ 15.2.0** |
| 编译行 | `g++ -std=c++17 -O3 -march=x86-64-v2 -static`（沿用 `tools_build.py` 的 CXX，未改） |
| 运行 | `python tools_build.py run <target> ...`（构建记录、源码漂移检查、退出码全部照旧） |

**这与 E5–E8 用的是同一套工具链**（那三轮就是 Windows 11 + MinGW-w64 g++ 15.2.0，
`data.hpp` 里 `make_data` 的 `noinline` 注释正是为它加的），E8 v4 之后才迁到 WSL。

## 3. 让装置在本机跑起来，只需要两处

### (1) vendored LITS 模型的一处 LLP64 不可移植写法

`vendor/lits_model_safe/lits_model.hpp:202`（`getCdf`）：

```c
static constexpr double min_double = 1. / (1UL << 52);
```

Linux/LP64 上 `unsigned long` 是 64 位，`1UL << 52` 合法；Windows/LLP64 上是 32 位，
GCC 直接**报错**（`right operand of shift expression ... >= precision 32`）。**`-fpermissive`
不是出路**：越界移位会折成另一个常数，而这个常数正是 CDF 的提前终止阈值，属于语义改变。

处置：`1UL` → `1ULL`。**在 LP64 上两者数值完全相同（2^-52）**，所以 WSL 下这份头文件的行为
不变，只有字节不同。该修改已写进生成脚本 `patch_lits_safety.py`（第三段编辑），脚本仍然
先断言 E4 记录的补丁哈希、再应用本编辑、再断言最终哈希：

- E4 验证过的字节：`b5a1c28251da7e74fd011e6b14a40927481c65ed24c837cc4892f26740af4c18`
- 树内的字节（= E4 补丁 + 本编辑）：`4f8674b576f6d4ce2045e5b5a2106c7a61cb830c4df88febfdfbd34641ac93e3`

复现：`python3 -B patch_lits_safety.py`（会重写 `vendor/lits_model_safe/lits_model.hpp`
与 `patches/lits_model_safety.patch`）。
**这就是本项目源码身份在本轮发生变化的那一处**，若要回到纯 WSL 身份，把这一 token 改回即可。

### (2) `build/art.o` 必须在 Windows 下重新编译

`build/art.o` 是**上一次 WSL 运行留下的 ELF 目标文件**，`tools_build.py` 的 `LIBS` 表把它
直接喂给链接器。MinGW 的 `ld` 不报错，而是产出**无法启动的 PE**：`.comment` 段被放在
`VA=0xc0000000`（低于 image base），Windows 加载器以
`WinError 193 / not a valid Win32 application` 拒绝执行。链接时唯一的征兆是
`ld: .comment: section below image base`。

处置：

```
gcc -std=c99 -O3 -march=x86-64-v2 -c third_party/vendor/libart/src/art.c -o build/art.o
```

（与 `tools_build.py` 的 `CC` 行一致。）凡是 `LIBS` 里引用 `build/art.o` 的目标，
在本机重建之前都必须先跑这一步；`C_SOURCES` 里带 `art.c` 的目标会自己重建它。

## 4. 本机跑通的证据（全部 rc=0）

| 检查 | 结果 |
|---|---|
| `tools_build.py diag_write_cost` + `run`（用法串正常返回） | 构建、运行均可 |
| `tools_build.py run check_lits_index mixed 8192` | **PASS**：171,972 keys / 77,480 操作返回 / 172,920 扫描记录 / 216 起点探针；LITS 10/15 区域映射、16,397/26,221 记录 |
| `tools_build.py check_combo` + `run check_combo mixed 32768 "" ctx16 same` | **PASS**：197,170/197,170 检查；`subblock_reclaims=67,732`、`reclaim_widened=1,128`（W 分支确实被走到） |

特别地，上面第二条说明 **LitsHpt 后端在本机不只是能编译，而是能跑完整门**；第三条说明
阶段 D 要诊断的 W（`subblock_reclaim + reclaim_widest`）在本机是**可达的**，不是空跑。

## 5. 可比性边界（重要）

- 本机时间数字（`erase_ms/insert_ms`、`w_*_ns`、墙钟）**只能与同轮、同机、同构建的记录比较**。
  与 WSL 各轮（g++ 13.3.0、Linux、`taskset` 绑核）**不可拼接**：编译器版本、CRT、分配器、
  页面行为、绑核方式全都不同。计数（`E25_DIAG` 的 `Counts`）是结构量，可跨环境读作"工作发生了
  多少次"，但**份额与比值仍以同轮同机为准**。
- 本轮诊断的判定标准本来就是同轮内部（计数指认 + 同状态 A/B），因此环境切换不改变判定形式；
  它改变的是"这些绝对时间能不能和 E3/E31/E37 的历史数字放在同一张表里"——**不能**。
- 绑核：Linux 走 `sched_setaffinity`，Windows 走 `SetProcessAffinityMask(1)`。新入口按计划要求
  **绑核失败即失败**；只有显式设置 `RES_ALLOW_UNPINNED=1` 才允许不绑核运行，且该状态会写进每行。

## 6. 本轮未做、需要回 WSL 时补的

- sanitizer 版（ASan/UBSan）：MinGW 无 `libasan/libubsan`，`E25_SAN_CXX` 也缺 clang，
  本机**跑不了**。计划要求的 sanitizer 门必须在 WSL/Ubuntu 下补跑。
- 与本文件同轮遗留的临时物（审批链路拒绝一切删除命令，无法清理）：
  工作区根目录 `tmp_probe.cpp/.exe`、`tmp_probe_static.exe`、`pe_check.py`、`pe_sections.py`、
  `copied_probe.exe`；`validation_e25_20260918/` 下的 `_dump.py`、`patch_res_targets.py`、
  `patch_script_edit.py`。它们不参与任何构建，可随时删除。

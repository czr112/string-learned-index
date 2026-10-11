# 执行环境与工具链核验（2026-10-10，阶段 D 入口诊断前置）

范围：本文件记录 2026-10-10 23:20–23:45 之间对“在哪台主机上跑阶段 D”所做的独立核验。
它不改索引、不改装置、不改写任何历史结果，也不提出候选。所有结论都在本会话内复现过，
命令与输出见各节。

## 1. WSL 在本会话不可用（需要提权，且审批通道故障）

- `wsl -l -v`、`C:\Windows\System32\wsl.exe -e bash -lc ...`、
  `%LOCALAPPDATA%\Microsoft\WindowsApps\wsl.exe`、`Start-Process wsl.exe` 四种方式一致返回
  `Wsl/E_ACCESSDENIED`（进程无法访问 WSL 服务/设备），不是“未安装”。
- 按沙箱规则改用提权重试，自动审批复核连续 4 次返回
  `Automatic approval review failed: stream disconnected before completion: stream closed
  before response.completed`——这是复核服务失败，不是“判定不安全”。因此本会话无法取得
  WSL 执行权；项目文档里“构建/运行使用 WSL”的路径在当前会话内不可达。

影响：依赖 WSL 的部分（sanitizer 门、绑核计时、与历史 WSL 计时并列比较）在本会话内不可执行。

## 2. Windows/MinGW 路线可用（独立复现，不用共享目录）

- 工具链：`D:\STM32\mingw64\bin`（g++ 15.2.0, x86_64-w64-mingw32）与
  `C:\Program Files\mingw64`（15.2.0, ucrt）两套都在本机可用。
- 在自建 scratch 目录里用
  `g++ -std=c++17 -O3 -march=x86-64-v2 -static -DE25_DIAG -DE18_COUNT -DE30_TIME -DE30_WRITE`
  构建 `diag_write_cost.cpp`，唯一需要的最小可移植性修改是
  `vendor/lits_model_safe/lits_model.hpp:202` 的 `1UL << 52` → `1ULL << 52`
  （LLP64 下 `unsigned long` 是 32 位，`1UL << 52` 在 MinGW 上是编译错误）。
  修复后编译与静态链接成功，无其它可移植性错误（源码对 `__linux__` 的依赖只有
  `diag_write_cost.cpp` 的 `sched_setaffinity`，且已被 `#ifdef` 包住）。
- 端到端：把 scratch 目录的 cwd 指向镜像目录（`../validation_e7_20260913/results/main/
  frozen_ov_mixed.json` 就位）后运行 `mixed 8192 1 "" smoke same`，rc=0，写出 4 行 JSONL；
  再跑 `mixed 2048 2 "" detcheck same`，两次重复的计数字段完全一致（`w_insert_calls`、
  `k_insert_attempts`、`k_overflow_inserts`、`k_entry_shifts` 全部逐行相同），
  说明数据/计数路径在该工具链上可复现。计时字段同时可读，但见第 4 节。
- 共享工作区里同一路线的记录：`logs/build_diag_write_cost.json`
  （`status: OK`、`returncode: 0`、`exe_sha256: 37d698e3…`、stderr 里是
  `D:/STM32/mingw64/bin/...` 的 ld 警告），时间为 23:30:31。

## 3. MinGW 没有 sanitizer 运行时（本机硬限制）

直接验证：`g++ -std=c++17 -O1 -g -fsanitize=address,undefined` 链接一个空程序失败：

```
ld.exe: cannot find -lasan: No such file or directory
ld.exe: cannot find -lubsan: No such file or directory
```

即 `tools_build.py` 的 `SAN_TARGETS`（san_check_* / san_diag_*）在本机无法构建。
计数/计时诊断可以跑，sanitizer 门不行；这一点与 `tools_build.py:51-53` 的注释一致。

## 4. Windows 构建会“静默不绑核”，计时只能定位，不能与 WSL 计时并列

`sched_setaffinity` 在 `#ifdef __linux__` 内，MinGW 构建既不会绑核，也不会打印
“affinity pin failed”一行。按阶段 D 计划 §3 “绑核失败不得静默继续并输出可比较计时”，
Windows 侧的 `*_ns` / `wall_ms` 只能作为“哪个环节占大头”的定位证据：
- 分析器应把 `timed` 与宿主机一起读，不得把 Windows 的绝对耗时与历史 WSL 数字并列；
- 阶段 D 的候选判定应以计数版为主，计时只用于选取“有可分辨成本依据”的环节。

## 5. 共享对象 `build/art.o` 的身份缺口（建议在收口时登记）

- `build/art.o` 已于 23:30:13 被 MinGW gcc 重建（COFF，machine 0x8664），
  原 ELF 目标文件被替换；此后 Linux/WSL 侧任何链接 `build/art.o` 的目标都会失败或错配。
- `tools_build.py` 对未列入 `C_SOURCES` 的目标（`diag_write_cost`、`diag_write_residual` 等）
  只把 `LIBS` 路径放进命令行，记录里的 `c_objects` 为空，对象字节不在任何身份记录中。
  建议：轮次收口时单独登记 `build/art.o` 的 SHA-256 与生成命令，或声明本机由 MinGW gcc 生成。

## 6. 并发写入与现场残留（如实记录）

- 23:38 前后另一路进程正在撰写 `diag_write_residual.cpp`（当前磁盘版本以
  “Stage D entry diagnosis … under W … maintenance time is roughly a third of the shipped B arm”
  开头，17,778 B @ 23:38:55，仍在增长）。本会话对该文件的一次等长追加写与对方的整文件
  重写发生交叠；随后对方覆盖了整文件。已复核：当前磁盘版本中不含本次会话的任何文字
  （无 “STILL spends” / “Census” / “sp_index_bytes” / “std::filesystem” 等标记），
  即本次会话未在该文件留下残留，也没有覆盖对方最终内容。
- 本会话自建的两个 scratch 目录仍在工作区根目录，未删除（删除属破坏性操作，
  本次提权审批同样被上述复核故障拒绝）：
  - `.tmp_mingw_probe/`（MinGW 复现构建，含 diag_write_cost_probe.exe、art.o 等）
  - `.tmp_probe_run/`（端到端冒烟运行的 cwd 镜像与输出）
  下一轮可安全删除；二者的路径均已核对在工作区根目录之下。

## 7. 对阶段 D 的建议（不改变计划本身）

1. 入口诊断（8 格计数 + 最小计时抽查）可以在 Windows/MinGW 上完成，但结果文档必须写明
   宿主机与工具链，并声明计时未绑核、不与 WSL 历史数字并列。
2. sanitizer 门在本机不可执行，必须显式记为 NOT EXECUTED（计划里“未执行单列 NOT EXERCISED”
   的口径正好适用），不能当作通过。
3. 若后续候选 X 需要 sanitizer 或绑核计时，需要先恢复 WSL 执行权（或换一台有 clang++/libasan
   的机器）；否则候选轮的接受判据只能停在计数层面。

## 8. 对并发的 diag_write_residual.cpp 的独立复核（只读）

本次核验副本取自 23:39:51 的磁盘版本（32,469 B），未修改源文件：

- 两个目标都能编译通过（MinGW 15.2，`-std=c++17 -O2 -static`）：
  `-DE25_DIAG -DE18_COUNT`（计数版）与 `-DE30_TIME -DE30_WRITE`（计时版）各自 rc=0。
- 计数版端到端冒烟（cwd 指向镜像目录，输出写到工作区外的 scratch 路径）：
  `mixed 8192 1 "" <tag> same ctx16 <out>.jsonl` → rc=0，写出 `t=hdr` 与 `t=cell` 行；
  头部行确认 W 生效（`subblock_reclaim=1,reclaim_widest=1`），其余候选开关全 0，
  并带 `key_hash` / `sched_hash` / `clock:"off"` / `calib_ns:0` 身份字段。
- 教训（仅与工具链有关）：用 `C:\Program Files\mingw64`（UCRT）做**动态**链接、而 PATH 里的
  libstdc++ 来自 `D:\STM32\mingw64`（MSVCRT）时，`std::ifstream` 的状态位会异常
  （`good()==true` 而 `is_open()==false`），会让“输出已存在”的检查误报。项目 CXX 行带
  `-static`，两者一致时行为正常；复核时不要混用两套 MinGW。

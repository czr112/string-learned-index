# 等深目录字节序核查：下一阶段计划的前置发现

日期：2026-10-11。核查基于本地提交 `3f8f016`；生产源码未修改。本次只运行最小语义反例，没有性能计时。

## 已确认的问题

`model/equal_depth_dir.hpp` 的 `predict_group()` 在首个不同字节处使用 `sep[i] < q[i]`。
WSL 和本机 MinGW 的本次构建均为 signed char；这与文件声明及 `compare()` 实现的 unsigned-byte 顺序不一致。
问题同时影响 EqualDepthDir 与 EqualDepthDirTrunc，不影响 Context16 的实现。

以四个单字节键 `01, 7f, 80, ff` 构建四组，前缀长度为 0，两种目录的阈值均为 `7f, 80, ff`。
独立参考直接对整数形式的阈值计数，不调用生产比较器或复制二分代码。

| 查询字节 | 正确组号 | 当前完整目录 | 当前截断目录 |
| --- | ---: | ---: | ---: |
| 01 | 0 | 3 | 3 |
| 7f | 1 | 3 | 3 |
| 80 | 2 | 2 | 2 |
| ff | 3 | 3 | 3 |

遍历 `00..ff`：两种目录各 **128/256 组号不符，1 处相邻组号下降**。
WSL g++ 与 MinGW g++ 结果相同。MinGW 仅改用 `-funsigned-char` 的机制控制为两种目录各 **0 不符、0 逆序**。
该控制仅定位原因；修复应显式使用 unsigned-byte 比较，不能把改变整个项目的 char 编译语义当成修复。

## 证据范围

- 已证实：当前两种目录在声明支持的字节域内不满足独立阈值计数与组号单调性契约。
- 尚未运行：这一反例在完整 Index 上的插入、扫描和更新序列；具体可观测失败应由下一阶段 P0 验证。
- 训练放置与查找共用错误映射，单纯的“插入后仍能 find”及两种目录互相对照不足以发现这一问题。
  既有域门也不能因包含高位键就推定跨 `7f/80` 的分支和多组有序输出已被覆盖。
- 既有三语料的 ASCII 性能记录继续绑定原身份；它们不能证明完整字节域正确。
  修复后构建是新身份，旧计时不能作为新二进制已经复测的证据。
- ASan/UBSan 无诊断只说明其检测范围内未发现问题，不替代有序映射的语义门。

## 文件与复现

相对 `02_Code_and_Experiments/validation_e25_20260918/`：

- `scratch_truncdir/order_audit_20261011/probe_unsigned_order.cpp`：最小反例。
- 同目录 `wsl_output.txt`、`native_output.txt`、`native_unsigned_control.txt`：运行输出。

```bash
g++ -std=c++17 -O2 scratch_truncdir/order_audit_20261011/probe_unsigned_order.cpp \
  -o scratch_truncdir/order_audit_20261011/probe_unsigned_order_wsl.bin
./scratch_truncdir/order_audit_20261011/probe_unsigned_order_wsl.bin
```

探针为诊断程序：退出码 0 表示运行完成，判定以 `mismatches` / `inversions` 为准；正式回归门须对非零失配返回失败。

SHA-256：

- `index.hpp`：`61592294a0b0a58ee9064c28fd7583941934eaa33d7c37e644dc0b649f765016`
- `model/equal_depth_dir.hpp`：`6755f3caef472a878805c330059cf184a007704e9bb4a165d40076b1a27cbe9b`
- 探针：`69b87336eef01647c58ad183ab6cfa73a12ccf9a42b74aadf3624fc8b394858d`

处置：先修字节序、补独立语义门，再进入 [下一阶段计划](../docs/NEXT_ACTION_PLAN_20261011_POST_TRUNCDIR.md)。

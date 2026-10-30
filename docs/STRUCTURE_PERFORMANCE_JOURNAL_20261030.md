# 结构性能执行日志：2026-10-30

## 本轮范围与实际完成

用户授权自主核对上游、结构诊断、测试、整理提交和推送。原 D 盘工作区没有被修改。Git 读取提交对象/版本差异成功，但 `status` 报必须在工作树内，显式进入 D 路径曾 Permission denied；没有改变 ACL、提权、换执行器重试拒绝内容。

已从原本可读的 Git 对象导出完整 main（61,900,236 B bundle），在本轮可写的 `C:\Users\Noctis\Documents\Codex\2026-10-31\task\project` 建立隔离副本。原 HEAD `0b0181b` 和之前 84 个领先提交全部保留。最初 bundle 没有 HEAD 符号引用，首次 clone 未 checkout；显式选择其中 main 后完成恢复。

随后以已允许读取的普通文件 API，独立比对原工作区 4,770 个 tracked 路径；没有缺失文件，三个 gitlink 目录未作为普通文件复制。发现的十份文件内容差异导入副本，经两轴审查、十项 current SHA 复核后提交为 `0499fe57150b58b597f9296fc1386ab093ec00d8`。这不是原工作区 `git status` 的成功结果，也没有证明全部子模块/忽略文件状态干净。

在明确排除构建树、外部依赖/子模块、旧资料包和操作系统目录后，另列 1,535 个未纳入当前提交树且未被 ignore 规则排除的本地路径。它是限定范围的文件清单，不能冒充整个原仓库状态。1,520 份通过文本/格式/凭据特征检查的历史证据和冻结源码已无损归档；两个 ELF、元数据和一份大日志未提交，原文件全部保留。详见 archives/local_evidence_20261030/catalog.json。

## 上游与兼容验证

- 模型 main：`3894c767338a865d2920ba5e21283827fea1d803`，无更新。
- benchmark main：`94ead07a45af7cb5dd33a2a708683ec2ba4411b6`，相对 `003a55bf` 新增一提交、15 文件。新建快照 77 文件逐一匹配官方 Git blob；旧 62 个未改文件也先核对原字节。未递归初始化模型 submodule。
- 新工具支持 doctor/demo/suite/require-all；C++ contract/runner/原负载未改。suite 参数值必须字符串，路径相对 suite 文件；严格共同能力矩阵应启用 require-all，quickstart 的预期 unsupported 在严格模式须为失败，不能据此删键或替代 baseline。
- 新只读 verifier 对正式 77 文件通过；独立损坏 CMakeLists.txt 的负对照返回 1，且仅报告该文件不匹配。
- MinGW GCC 15.2 / CMake 4.2.1 / Python 3.13.12 原生构建完成；doctor 通过。
- `PYTHONUTF8=1` 下统一入口 65 项与新增 suite 15 项通过。初次默认 GBK 的失败日志保留；未改测试源码以隐藏编码失败。
- lookup_workload 9 项通过。real_dataset 共 6 项，最终仍有 1 项 WinError 32（`.part` 文件被占用）错误；初次默认临时目录 PermissionError 也保留。
- 原生 unified_runner C++ 检查未过：包含微小 fixture 的 `throughput lookup duration is below clock resolution`、finite-domain 检查、CLI 输出独占创建失败。换为合法工作区临时目录后仍失败，不继续重复碰运气。它们不是新旧索引性能差异证据。
- legacy TLI/Boost 测试未构建。完整标准 Linux 回归未完成。

WSL 首次启动返回 `Wsl/E_ACCESSDENIED`，未再次尝试，也没有通过另一执行器访问原路径。固定 HOT 的原生编译探针因 `uint`、LP64/POSIX 依赖失败；没有移植固定库或声称 sanitizer 通过。

## 已关闭的测量与诊断结论

比旧检查点更新的 paired 校准已经完整执行且 `qualified=false`。本轮只归档其 68 份文本记录，八份 sample SHA、report SHA 和归档逐成员 SHA 均核对一致。没有候选延迟测量、没有追加样本；原绝对门仍失败。详见新 paired 报告。

读取当前本地 HOT adapter，确认无长度上限声明、扩容后 record key 未 rebasing、update 使用旧指针、删除记录可能在 rebuild 中复活，以及非空 trie 移动赋值丢失旧 root 所有权的路径。它们尚未完成 Linux 动态复现；HOT 暂不能用于新的可信动态排名。未将未编译的所有权重构提交为“修复”。

BM/ART/HOT 的匹配布局诊断、下一轮单一假设和停止条件已写入 [长期计划](STRUCTURE_PERFORMANCE_PLAN_20261030.md)。当前不能从块探测下降或 resident 空间推导时延/内存流量收益。没有重开已失败的动态16、固定8维护、root 或预留容量机制。

## 发布和续接

Git HTTPS 连接经既有 `127.0.0.1` 代理失败；没有关闭代理或改写 84 个提交来绕过。GitHub connector 的只读复核仍可用：远端主仓库 main 保持 `6229ccc1cab196baa92565575a88320f479ec109`。尚未推送。

执行服务器曾短暂断开，失败的负对照/提交命令没有创建进程。恢复后先核实负对照路径不存在、隔离 HEAD 仍为 0499fe5，再执行；没有重复运行已完成的测量。

下一步是完成新文件审查与隔离副本提交，保留精确历史的 bundle；在合法 Git 网络和 Linux 测试环境恢复后，再分别推送与执行 baseline 修复验收。原 D 盘 main 仍为 0b0181b 且原修改保留，不能对其直接 reset/pull 覆盖。同步要求见发布记录。

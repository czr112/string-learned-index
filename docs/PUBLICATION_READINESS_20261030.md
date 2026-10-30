# 发布准备与原工作区同步

本轮实际执行位置是 `C:\Users\Noctis\Documents\Codex\2026-10-31\task\project`。它来自原本允许读取的 main Git 对象，以及逐文件允许读取、明确导入的十份修改；没有改写原 D 盘工作区或其 Git 元数据。

## 已有历史与本轮整理

- 原本地 main：`0b0181b69ba7e1ff573c4ca3806ae24e4f19afe8`；当时领先实时 GitHub main 84 提交。
- 实时 GitHub main：`6229ccc1cab196baa92565575a88320f479ec109`。本轮尚无成功推送。
- 本轮首个提交 `0499fe57150b58b597f9296fc1386ab093ec00d8`：整理十份已读文件，保留 historical recorded_at_run，补导航和 runner 纪律。两个独立审查轴均无阻断项。
- 待发布内容另含：历史文本证据无损归档和两份原先未入库导航文档、paired 校准失败收口、长期计划和日志、最新 benchmark 工具身份与兼容回归记录。

原 84 个提交的只读发布检查涉及 818 变更文件、tip 中 14,853,644 B，最大新增文件 1,023,490 B。23 份新增 tar.gz 成员名检查没有发现构建二进制或键数据集；常见凭据签名扫描未命中。它是限定审查，不是完整安全证明。

本轮另归档 1,520 份文本/冻结源码：20,319,876 B 压缩为 3,340,887 B，并逐成员复核 SHA。两个 extensionless ELF、六个 .DS_Store、三个冻结 .gitignore、一份 30,338,576 B 编译日志和根目录空探针没有作为新产物发布；原件未删除。catalog 记录保留路径、SHA 和原因。新 paired 包仅含 68 份文本记录。

仓库 `.gitattributes` 为 `* -text`，原始证据按字节保留。格式检查对这些文件使用 `cr-at-eol`，并允许历史原文的 EOF 空行；不得通过全树换行转换破坏原始 report/sample SHA。

## 当前阻塞不是审批等待

1. 原工作树：`git status` 无法建立 worktree；显式进入 D 路径曾返回 Permission denied。原仓库完整 Git 状态未获确认，不能把隔离副本干净称为原仓库整理完成。
2. Linux：`wsl.exe ... /usr/bin/g++ --version` 返回 `Wsl/E_ACCESSDENIED`。没有重试、提权或转用另一执行器绕过。HOT 普通/sanitizer 与项目标准 Linux 门未执行。
3. 传输：`git ls-remote https://github.com/czr112/string-learned-index.git refs/heads/main` 报经 127.0.0.1 无法连接 github.com:443。发布操作本身尚未尝试，未出现发布审批拒绝。不能把 connector 中重建新 SHA 的提交替代精确保留的 84 个历史提交。

没有进行 force push、reset、clean、历史重写或原文件清理。

## 合法恢复后的步骤

先在隔离副本查看 `git status --short`、提交日志、归档哈希及检查记录；确认没有变化后，在可连接的 Git 环境核对远端 main。只有远端仍为已审阅祖先时，才执行普通 fast-forward push；若远端前进，先审查新差异，不强推。

一旦发布隔离副本的新提交，原 D 盘 main 将落后于远端，原 ten-file 修改仍留在原工作树。如果期间原仓库又产生提交，也可能分叉。不要在原目录直接 pull/reset 或复制整个隔离目录覆盖它。

恢复原目录的合法可写执行上下文后，先保存当前 diff、未跟踪文件清单及必要备份，重新核对真实状态；逐文件识别已被新提交吸收的十份修改和后来新增修改。只在备份与对应关系明确后进行 fast-forward/有审阅的整合，并保留未吸收改动。存在冲突则保留冲突和备份，不丢弃本地状态。

原 bundle `project_main.bundle` 只覆盖最初 0b0181b；本轮完成后的新 bundle 应以最终 main 另行生成，不能把原 bundle 当作本轮新增提交的备份。

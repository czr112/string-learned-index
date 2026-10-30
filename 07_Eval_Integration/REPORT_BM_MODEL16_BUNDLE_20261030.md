# Model16只读快照独立源码包与干净重建（2026-10-30）

登记fa46c54，源码归档补齐06d1b71，双轴复审通过。导出的C++/头文件逐字节对应已验收421eb36的真实contract依赖闭包；原始依赖日志、身份和每份源码均核对哈希。没有调整模型、能力、数据或生产默认。

源码包含13个实际编译依赖文件，加独立Python构建器、说明、来源与许可等，共18个清单文件及manifest.json，归档19项逐成员SHA校验通过。源码包排除二进制、数据集、Git元数据及构建日志；验证日志另存。

导出目录内独立构建器只用Python标准库和系统GCC，不导入原工作区工具、不调用Git或网络。实际-MM闭包全部位于导出目录，使用相对文件名，移除环境附加include/library路径；来源、编译器/助手、参数和二进制均记录身份，临时二进制经原子发布并在运行前后复核。普通与非恢复ASan/UBSan/LSan干净重建均通过同一公共契约127/127。

这是源码/契约重建，未重做全32格、故障或空间矩阵；这些验收来自同字节源码的既有独立证据。系统工具链和标准库仍是正常依赖，不宣称完整工具链密闭。便携版本使用相对路径闭包SHA，因此描述符的构建身份应读取包内实际记录，不能要求与原绝对路径构建的二进制逐字节相同。

## 取用

[仅源码包](results/bm_model16_snapshot_bundle_20261030/source_bundle.tar.gz) · [逐成员SHA](results/bm_model16_snapshot_bundle_20261030/source_bundle_members.json) · [导出与重建结果](results/bm_model16_snapshot_bundle_20261030/export_report.json)。解包后按README分别运行rebuild.py的普通/sanitizer命令，输出目录必须为新目录。包仅作为本地私有复现材料；第三方原文许可和来源说明保留，没有公开上传。

功能和空间验收继续成立，但未证明延迟收益。动态16与固定8段维护方向保持停止，Context16保持默认。用户已表示可安排本机空闲测试窗口；开始时间/持续时长尚待确认，新的资格校准只会在该条件下另立登记，旧失败不撤回、不混样，也不放宽门槛。

[协议](../docs/PROTOCOL_BM_MODEL16_BUNDLE_20261030.md) · [审查](../docs/REVIEW_BM_MODEL16_BUNDLE_20261030.md)。validation_records.tar.gz保存两种构建的命令、原始日志与身份，validation_members.json逐项SHA并解包复验，不含可执行文件。

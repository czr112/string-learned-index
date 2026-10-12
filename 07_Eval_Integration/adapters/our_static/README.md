# StaticPhysicalIndex 只读接口

工厂 `make_our_static_index({})` 创建全新只读索引，成功 `bulk_load` 后提供 `find` 和自持有、有序的 `scan`。
它复制必要键值，不保留输入引用；扫描结果可在索引销毁后继续使用。生产动态 Index 与默认 Context16 不变。

可选参数只有 `lookup`：`learned`（默认）、`directory`、`wide_directory`（显式实验候选）。
后者仅在预测证书宽度超过3块时，在该证书范围内二分完整 fence；不根据语料名称选择策略。
统一评测器传空参数时测量默认 learned。候选对照驱动明确传入 `lookup=wide_directory`。

输入须为 unsigned-byte 字典序严格递增的唯一键。支持空键、NUL、高位字节、前缀关系与完整 uint64 值域。
单键长度、后缀 arena 总字节数和物理块总数分别不超过 UINT32_MAX。超界抛 length_error；构建失败不发布半成品。
一次成功构建后不允许重新 bulk_load。构建前的数据操作抛 logic_error。
插入、更新、删除及 range_sum 抛 UnsupportedOperation，保持状态不变。

内存分开计核心容量与适配器对象；保留输入字节为0。分配器开销、RSS、构建峰值未知，扫描返回值不计入常驻索引。
本轮没有 ART 完整空间记账，不能据此宣称空间优于 ART。

WSL/Linux 独立验收：

```bash
cmake -S 07_Eval_Integration/adapters/our_static -B /tmp/static-adapter-build -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD_ACCEPTANCE=ON
cmake --build /tmp/static-adapter-build
/tmp/static-adapter-build/our_static_acceptance
```

完整结构门、sanitizer 与配对比较：

```bash
python3 -B 07_Eval_Integration/diagnostics/run_static_interface.py --output static_interface_new_name
python3 -B 07_Eval_Integration/diagnostics/verify_static_interface.py 07_Eval_Integration/results/static_interface_new_name
```

输出名不可覆盖。原型与生产对照文件未修改；候选判定见本轮 RESULTS 报告。

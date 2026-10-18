# StaticPhysicalIndex D1 显式只读候选

选择本适配包即启用一层簇内前缀辨识；不改变生产Index、Context16或原our_static默认。
工厂`make_our_static_refined_index({})`原生支持一次bulk_load、精确find、自持有有序scan。
可选`lookup=refined`（本包默认）或`directory`（同布局目录控制）；未知/重复参数拒绝。
写入、更新、删除和range_sum抛UnsupportedOperation。它尚未获得最终采用资格。

支持空/NUL/高位字节/前缀键和完整uint64值。键长、聚合后缀arena、块数分别受UINT32_MAX限制；
输入须按无符号字节严格有序唯一。索引持有所需键值，构建失败不发布状态，scan结果可跨析构。
内存计对象和容量，含局部前缀/模型；不含分配器、RSS、构建峰值及返回scan结果。

manifest绑定四份实现头文件，CMake逐项核对并计算完整bundle与适配器身份，声明过期则构建失败。
实际依赖还包括评测仓库index_contract.h；各轮结果另记录编译依赖和固定评测器身份。
实现引用项目内冻结研究源码，不复制或修改D1算法。

WSL/Linux：

```bash
cmake -S 07_Eval_Integration/adapters/our_static_refined -B /tmp/refined-adapter-build -DCMAKE_BUILD_TYPE=Release -DREFINED_BUILD_ACCEPTANCE=ON
cmake --build /tmp/refined-adapter-build
/tmp/refined-adapter-build/our_static_refined_acceptance
```

统一评测器可直接以本目录作为`--implementation`；性能采用结论以E阶段结果为准，发现流程的默认重复数不能作为排名依据。

E1正式外部验证已完成：[八格结果](../../RESULTS_REFINED_EXTERNAL_20261018.md)。本轮无清晰集成回归，长前缀有收益，tranco/64k仍落后ART；泛化、完整空间/峰值及最终采用资格待核验。

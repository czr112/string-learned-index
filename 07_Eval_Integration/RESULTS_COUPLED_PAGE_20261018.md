# F5：联合点查与dirty维护的同步换根

F5的根/页同步替换语义、空间入口与最终完整点查必要门均通过。
六格完整点查为同当前map D1的0.985–1.046倍，960条计时已独立复核；允许继续F6动态验证。
本轮是flat根摘要数组与single-page完整点查的联合方案；F4的
`selected_single_mechanism=null`保留。生产默认Context16不变。

协议：[F5预登记](diagnostics/F5_COUPLED_PAGE_SPEC_20261018.md)。
实现：[coupled_page_index.hpp](diagnostics/coupled_page_index.hpp)、
[coupled_root.hpp](diagnostics/coupled_root.hpp)。F3/F4/F4M的冻结源码和结果均未修改。

## 同步发布与语义

map继续持有页，flat根拥有separator并借用当前页指针。
槽内插入、值更新、删除保持页与根地址；替换、分裂、合并先准备新页与整版新根，
再以不分配的map节点转移和noexcept根交换提交。这里只提供单线程异常安全。
verify在解引用根指针前，先核对所有separator与页指针和持有map一一对应。

普通O2和ASan/UBSan的输出完全一致，每种构建12,529,479项检查：

| 组 | 每种构建检查项 |
| --- | ---: |
| 合成域/随机写/失败回滚 | 2,068,642 |
| tranco64 | 2,059,403 |
| mixed | 4,160,561 |
| skew | 4,240,873 |

新增8个根准备失败场景，覆盖分裂、满槽替换、compact、相邻合并的prepare前/完成后失败；
旧根、页版本、数据和dirty成员均保留。完成但未发布的根也计入复制项与已知峰值。
保留F4M的五个dirty失败场景及原页分裂/替换失败回归。
65,536个两字节查询验证NUL、高位字节、摘要碰撞和页界选择；全删后只剩单空页根。
三语料与随机写都对照独立std::map和冻结F4M，核对完整点查与owning scan。
干净页与1024纯值更新后compact零重建；8192键单页删除只重建2047行。

## 空间与根复制账

| 语料 | F5初建自计bytes | 同数据D1 bytes | F5/D1 |
| --- | ---: | ---: | ---: |
| tranco64 | 2,231,007 | 2,066,217 | 1.079754 |
| mixed | 13,746,070 | 13,142,853 | 1.045897 |
| skew | 13,687,832 | 13,073,458 | 1.046994 |

三格均在预登记1.10以内。真实语义轨迹包含1024既有键update和1600混合写，再清理dirty页。
根发布与复制计数包含初始化，不是点查计时轨迹的1024纯值update成本：

| 语料 | 页重建 | 根发布 | 根项复制 | separator逻辑字节复制 | 最大根项 | 最终根bytes | 已知自计峰值bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| tranco64 | 95 | 96 | 5,327 | 71,586 | 60 | 3,279 | 2,544,032 |
| mixed | 164 | 165 | 17,531 | 2,034,549 | 114 | 18,864 | 14,838,728 |
| skew | 164 | 165 | 17,531 | 2,710,366 | 114 | 23,279 | 14,608,775 |

compact目标数分别60/110/110，均不超过初始dirty页数；单事件最多两个源页、2049行。
每次换页仍复制O(页数)根目录，整个写路径尚未局部化。
已知自计峰值包含旧/新根和已知行/页缓冲，排除map/set节点链接、分配器元数据和内部builder scratch，
不是完整分配峰值。失败中途尚未完成的根部分复制也没有精确计数。

## 最终读预算

三语料各做clean及1024次既有键update；D1由同一当前map重建。
查询沿用F3输入和10%缺席构造，运行完整find，不用F3/F4跨轮比值相乘。
四臂live/live_AA/D1/D1_AA，每格5进程×8轮，Williams顺序固定，CPU0、O3、x86-64-v3。
全部六格普通/ASan预检通过（每种构建2,362,826项），才开始960条计时。
每进程先取8轮配对比值中位数，再取5进程中位数；不是独立ns/op中位数相除。

| 语料/状态 | F5/D1配对耗时比 | 慢于D1进程 | 双方AA带 | 必要读预算 |
| --- | ---: | ---: | ---: | --- |
| tranco64 clean | 1.045793 | 5/5 | 1.191101 | 在1.15以内 |
| tranco64 update后 | 1.015319 | 5/5 | 1.175943 | 在1.15以内 |
| mixed clean | 0.985002 | 2/5 | 1.139753 | 在1.15以内 |
| mixed update后 | 1.039410 | 4/5 | 1.084141 | 在1.15以内 |
| skew clean | 1.016284 | 3/5 | 1.086636 | 在1.15以内 |
| skew update后 | 1.041231 | 4/5 | 1.106500 | 在1.15以内 |

六格预登记中位数均<=1.15，没有failed/unresolved格，dynamic_gate_allowed=true。
AA带特别是tranco较宽，所有差异都在AA带内；本轮只能确认测得的中位数满足进入下一阶段的预算，
不能声称加速、等价性或真实最坏开销上界。没有追加重复，F4单因素未通过判定也未变。
这份读轨迹只有既有键update，页拓扑没有改变、根只初建一次；不能替代结构写后的动态矩阵。

完整F6动态吞吐、维护/扫描、事件暂停、Context16/ART/HOT同能力比较和完整分配峰值仍待执行。
后续[统一动态接口入口F6A](RESULTS_COUPLED_ADAPTER_20261018.md)也已完成语义验收，
现按[动态矩阵计划](../docs/NEXT_ACTION_PLAN_20261018_DYNAMIC_MATRIX.md)进入F6B准备；生产默认不变。

## 证据与复现

语义：[identity](results/f5_coupled_page_20261018/identity.json)、
[gates](results/f5_coupled_page_20261018/gates.json)、
[空间入口](results/f5_coupled_page_20261018/entry_decision.json)、
[独立复核](results/f5_coupled_page_20261018/independent_verification.json)。
点查：[身份](results/f5_coupled_page_read_20261018/identity.json)、
[原始960条计时](results/f5_coupled_page_read_20261018/raw.json)、
[逐进程比值/AA](results/f5_coupled_page_read_20261018/summary.json)、
[决定](results/f5_coupled_page_read_20261018/decision.json)、
[独立复核](results/f5_coupled_page_read_20261018/independent_verification.json)。
目录内保留完整命令日志、冻结源码与二进制身份；编译产物按仓库规则忽略。
独立复核确认F4叶页身份未改，F4M的insert/update/erase/scan/compact及dirty维护函数逐字相同。

```bash
python3 -B 07_Eval_Integration/diagnostics/run_coupled_page.py --output UNIQUE_NEW_SEMANTIC_DIR
python3 -B 07_Eval_Integration/diagnostics/run_coupled_page_read.py --entry UNIQUE_NEW_SEMANTIC_DIR --output UNIQUE_NEW_READ_DIR
python3 -B 07_Eval_Integration/diagnostics/verify_coupled_page.py 07_Eval_Integration/results/UNIQUE_NEW_SEMANTIC_DIR --timing 07_Eval_Integration/results/UNIQUE_NEW_READ_DIR
```

以上为复现命令，不授权追加本轮重复来改变固定停止判定。

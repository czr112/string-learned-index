# F6A统一动态接口入口（不计时）

前置：F5语义/空间和六格完整读必要门均通过，960条计时已独立复核。
本轮只增加独立实验v1 bridge，不改冻结F5核心，不注册成生产默认，也不声称已经完成F6动态矩阵。

工厂make_coupled_page_index只接受空参数，固定F5联合路径与15/16占用率。
bulk_load/find/insert/update/erase/owning scan支持，range_sum明确unsupported。
bulk_load用临时向量复制contract Row，scan向contract Row移动自有键，成本不得从后续构建/scan账本中删除。
dirty compact仅是实验接口扩展，后续必须单列且计入维护时间；v1没有此统一操作，不伪称外部基线提供同一compact。
描述符绑定F5叶/根/页索引及底层依赖bundle哈希、adapter文件哈希。

使用冻结的adapters/common/contract_acceptance.cpp，O2及ASan/UBSan均必须0失败，输出完全一致。
补充真实三语料的bulk_load、1024既有update、插入/删改/重插、完整点查和owning scan，
逐步对照独立std::map以及冻结F5裸核心；compact前后必须一致。
先冻结来源/输入/命令/二进制身份。无吞吐与采用判定；大规模动态矩阵另行预登记。

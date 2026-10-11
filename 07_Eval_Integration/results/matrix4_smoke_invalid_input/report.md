# 字符串索引统一评测报告

本报告覆盖单线程、内存、唯一键 map 的静态点查询。首版只验收 `bulk_load` 和 `find`。

## 数据与负载

- Reader：auto
- 原始记录：不可用；排序去重后键数：不可用；重复记录：不可用
- 逻辑哈希（SHA-256）：`None`
- 负载：`static-uniform-point/v1`；请求数：不可用；受控未命中：不可用；重复：3

## 实现结果

| 实现 ID | 状态 | 阶段 | 原因 | 构建中位数（ns） | 吞吐量中位数（ops/s） | 吞吐量范围（ops/s） | 吞吐量标准差 | P50 延迟（ns） | P99 延迟（ns） |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| invalid-1 | invalid_input | dataset | dataset metadata read failed: No such file or directory | 不可用 | 不可用 | 不可用–不可用 | 不可用 | 不可用 | 不可用 |
| invalid-2 | invalid_input | dataset | dataset metadata read failed: No such file or directory | 不可用 | 不可用 | 不可用–不可用 | 不可用 | 不可用 | 不可用 |

## 解读限制

失败、跳过和不支持的实现保留在所有汇总文件中，性能列为空，不以零值代替。内存分项只有在适配器给出明确口径时才展示；本报告不据此生成内存排名。延迟包含计时器开销，进程未绑核，小规模结果不用于论文级排名。

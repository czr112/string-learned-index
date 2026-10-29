# U3 实现（第 1 步）：`Page::split_at_block` 通过单元测试

日期：2026-10-29。按 [U3 前置核对](U3_PREREQ_20261029.md) 动工，实现"整页切分不重编码"的核心函数。

## 实现

新增 `Page::split_at_block(std::uint32_t b_split)`：把块 `[b_split, count)` **移出**到新页。

| 步骤 | 做法 |
|---|---|
| 块与 fence | `blocks_`/`stable_fences_` 的选中区间**整体移出**（不重编码） |
| arena | 新页建**压实后的** arena，逐 entry `memcpy` 后缀并**重写 offset** |
| region | 新页 1 个 region：沿用 `prefix`、`first=0`、`count=移出块数` |
| fences/digests | 新页各 1 条（`prefix + 首块首后缀`）；`fence_prefix_` = 该 fence 长度；`digest8("")` |
| `size_` | 两页各自按移出的行数调整 |
| `bm_` | 两页都用新的块边界**重拟合**（拟合只占 3.7%，可接受） |

## 单元测试

对 2,048 行整页在三个切分点各做一次，检查"两半的 scan 拼接后与原始逐行相同"，
且"每个键在两半中恰好一边命中原值"：

| 切分块号 | 下半行数 | 上半行数 | 失配 |
|---:|---:|---:|---:|
| 70 | 1,050 | 998 | **0** |
| 35 | 525 | 1,523 | **0** |
| 100 | 1,500 | 548 | **0** |

**语义门**（三语料 + 增删改 + 零分配）：263,469 项检查、**零失配**，
说明新函数没有影响既有路径。

## 意义

这是"页满重建"降本的关键一步：现在可以在**不物化 2,048 个
字符串**的前提下把一页切成两页，正好绕过占重建成本 **64%–77%** 的 `scan`。

## 尚未做（下一步）

1. **接入 `Index::rebuild`**：页满分支改走 `split_at_block`（两半都构造成功后才释放旧页，
   保持强异常保证）；
2. 测重建单价（门：**−25%**，预期 −60%～−75%）；
3. 三语料语义门 + 失败注入 + 空间 + 查询回退复测。

## 状态

| 项 | 状态 |
|---|---|
| `split_at_block` 实现 | ✅ 单元测试三个切分点零失配 |
| 既有路径回归 | ✅ 三语料语义门零失配 |
| 接入 `rebuild` | ⬜ 待做 |
| 收益测量 | ⬜ 待做（预期重建单价 −60%～−75%） |
| Context16 默认 | ✅ 不变 |

## 产物

- 实现：[bm_n1_page_core.hpp](diagnostics/bm_n1_page_core.hpp)（`split_at_block`）
- 测试：[check_bm_split_block.cpp](diagnostics/check_bm_split_block.cpp)
- 运行：[tools/u3_split_block.sh](../tools/u3_split_block.sh)

# U3 第 3 步（接线）遇阻：`Page` 拷贝被禁用，需改两阶段切分

日期：2026-10-29。尝试把 `split_at_block` 接进 `Index::rebuild` 的页满分支，**编译即失败**，已回滚接线。
`split_at_block` 与其单元测试保留（已验证，零失配）。

## 两个编译期阻塞

1. **`Page` 的拷贝构造被显式删除**：

   ```cpp
   Page(const Page&)=delete;Page& operator=(const Page&)=delete;   // 第 224 行
   ```

   我原打算"先拷贝一份、在副本上切分"，以保证在 `prepare_root` 抛异常时原页不被破坏。
   **该路线不可用**。

2. `try_insert_known_absent` 已随 T2 回滚不存在，接线里应改用 `try_insert`。

## 为什么会走到这一步

现成的 `rebuild()` 用 `staged` 暂存与 `std::swap` 完成"构造成功后才替换"，依赖的是
**新页由 rows 构造**（因此天然是新对象）。而 `split_at_block` 是**就地修改**源页，
与"先构造、后替换"的强异常保证天然冲突。

## 正确做法：把切分拆成两个阶段

| 阶段 | 签名 | 是否可抛 | 作用 |
|---|---|---|---|
| 1 提取 | `static std::unique_ptr<Page> extract_upper(const Page& src, std::uint32_t b_split)` | **可抛** | 用 **const 源页**构造上半个新页：压实 arena、搬块、重建 region/fence/digest/`bm_`。**不改源页** |
| 2 丢弃 | `void drop_upper(std::uint32_t b_split) noexcept` | 不抛 | 在 root 就绪后，把源页的 `[b_split, count)` 块与 fence 删掉、`size_` 调整、重拟合 `bm_` |

接线顺序：

```cpp
auto upper = Page::extract_upper(*target->second.core, b);   // 可能抛，源页完好
// 把新键插进所属的一半（upper 或源页）——插入本身可能重建，仍走旧路径
Pages staged; ...
auto next_root = prepare_root(target, pages_.end(), staged, added);  // 可能抛，两页都还是候选
target->second.core->drop_upper(b);                          // noexcept
root_.swap(next_root);
pages_.emplace(upper_key, Node{std::move(upper), ++generation_});
```

`extract_upper` 与现有 `split_at_block` 的区别只有一处：**它不修改 `src`**，
所以 arena 压实时要从 `src.arena_` 读、往新页写，块的搬运也是复制而非 `std::move`。

## 状态

| 项 | 状态 |
|---|---|
| `Page::split_at_block` | ✅ 已实现并通过单元测试（三个切分点零失配） |
| `Page::block_count()` | ✅ 已加 |
| 成本对比（微基准） | ✅ −60.7% / −73.5% |
| **接线** | ❌ **回滚**：需先改两阶段切分 |
| 代码状态 | 索引恢复为 S0 验收通过版本；页核心保留新增的两个成员函数 |
| Context16 默认 | ✅ 不变 |

## 下一步（明确、有限）

1. 把 `split_at_block` 重构为 `extract_upper(const Page&, b)` + `drop_upper(b) noexcept`；
2. 按上面顺序接进 `rebuild` 的页满分支；
3. 过三语料语义门 + 失败注入 + 空间 + 查询回退，再测重建单价。

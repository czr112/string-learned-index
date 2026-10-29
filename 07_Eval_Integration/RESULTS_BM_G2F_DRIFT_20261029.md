# G2f：单次分裂后四数一致，但多次操作后仍报 `size/memory`

日期：2026-10-29。

## 已验证

用最小用例（一次分裂后立刻读四数）：

```
after build: size=65536 live=65536 resident=1944569 memory=1944569 pages=32 bindings=32
after split: size=65537 live=65537 resident=1985516 memory=1985516 pages=33 bindings=33
```

**单次分裂后完全一致**（`resident_ = memory_bytes()` 重算有效）。

## 仍然失败

三语料门（会 `verify()`）仍报 `size/memory`。由于 `rebuild()` 与 `rebuild_by_split()` 末尾
都已改成 `resident_ = memory_bytes()`，剩余不一致只可能来自 `verify()` 的**第一条判据**
`live == size_`（`live` 为各页 `size()` 之和），或来自**未被重算覆盖的第三条路径**。

最可疑的两条（下一轮按顺序查）：

1. `erase()` 与小页合并走 `rebuild(..., coalesce=true)`，该路径末尾虽重算，但
   **合并会销毁一个页并把它并入邻页**，若 `lower_key`/`upper_key` 的页表项与页内 `size_` 失配，
   `live` 就会与 `size_` 差；
2. 连续多次分裂后，`block_for` 的切分点与 `compare(key, upper_key)` 已统一，但
   **`upper_key` 可能等于 `lower_key`**（空后缀块），导致 `pages_.emplace` 与后续插入落到同一键。

## 下一步（精确）

把四数 dump 挂到**每一次** `insert`/`erase` 之后（而不是只在分裂后），
打印首个 `live != size_` 或 `resident != memory` 的操作序号与类型，即可一次定位。

## 状态

| 项 | 状态 |
|---|---|
| 单次分裂记账 | ✅ 四数一致 |
| 多次操作后 | ❌ `size/memory` |
| 接线 | ⛔ 回滚，`n3_gate` 恢复全绿 |
| Context16 默认 | ✅ 不变 |

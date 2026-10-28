"""One-shot doc updates for the encoding audit round."""
import pathlib

spec = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/diagnostics/BM_ENCODING_AUDIT_SPEC_20261019.md')
text = spec.read_text(encoding='utf-8')
text = text.replace(
    '## 3. 结果摘要（执行后填写）\n\n见[执行结果](../RESULTS_BM_ENCODING_AUDIT_20261019.md)。',
    '## 3. 结果摘要（执行后填写）\n\n'
    '窗口均值17–26块（URL p95 73、max 137），窗口覆盖98.2%–98.6%、缺席窗外仅1.3%–1.9%；'
    '同坐标簇占比 words 0.39%/URL 44.3%/短语25.2%/tranco 0.82%，簇内额外前缀中位 0/11/1/0 字节；'
    '预测块=所在块 8.6%–9.9%；`certified()` 0/32。'
    '**`encoding_refinement_justified=true`，仅 `memetracker_urls`。**详见[执行结果](../RESULTS_BM_ENCODING_AUDIT_20261019.md)。')
spec.write_text(text, encoding='utf-8')

state = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/07_Eval_Integration/STRUCTURE_OPTIMIZATION_STATE.md')
text = state.read_text(encoding='utf-8')
anchor = '- 原BM接入、真实键泛化、页内短目录、完整上游参照与短目录完整读门均已完成，预测前驱括号关闭。'
assert anchor in text
bullet = ('- **阶段3失辨审计完成：只有URL类键值得动编码**：[四格窗口/簇/回退分布](RESULTS_BM_ENCODING_AUDIT_20261019.md)。'
          '按BM叶页真实构造逐页重建（2048/15/`fit(1,7,8,1)`），四格窗口均值17–26块、p95最高73、max 137，'
          '窗口覆盖所在块98.2%–98.6%，缺席键落在窗外仅1.3%–1.9%——代价在窗口宽度而非回退分支。'
          '同坐标簇占比 words 0.39%、**URL 44.3%**、短语25.2%、tranco 0.82%；簇内额外公共前缀中位分别为0/11/1/0字节。'
          '预测块=所在块8.6%–9.9%，`certified()` 0/32。判据`encoding_refinement_justified`仅对`memetracker_urls`成立，'
          '短语因额外前缀中位仅1字节、words/tranco因几乎无簇而不适用。\n' + anchor)
text = text.replace(anchor, bullet)
text = text.replace(
    '预测块族已两次入口失败并关闭。下一步按计划做**cost/adaptive planner**独立消融（成本单位先按真实叶页标签批次换算），或按阶段3先审计真实数据的失辨分布；短目录作为组件证据不再单独调参。',
    '预测块族已两次失败并关闭，阶段3失辨审计已完成：唯一有登记依据的是**URL类键的一层簇内局部前缀细分**（words/tranco为阴性对照、短语为低收益对照）。下一步登记该候选的工作入口；cost/adaptive仍单列，不与编码细分同轮启用。')
state.write_text(text, encoding='utf-8')

readme = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/README.md')
lines = readme.read_text(encoding='utf-8').split('\n')
out = []
replaced = 0
for line in lines:
    if line.startswith('- **当前优先任务**：'):
        out.append('- **阶段3失辨审计完成（10月19日）**：[四格窗口/簇/回退](07_Eval_Integration/RESULTS_BM_ENCODING_AUDIT_20261019.md)：窗口均值17–26块、覆盖98%以上、缺席窗外仅1.3%–1.9%；同坐标簇仅URL显著（44.3%，簇内额外前缀中位11字节），故局部前缀细分只对URL类键有登记依据。')
        out.append('- **当前优先任务**：[URL类键的一层簇内前缀细分](docs/LONG_TERM_BM_PLAN_20261018.md)。完整原生参照、短目录完整读门、预测块族、阶段3审计均已完成；下一步登记URL专用候选的工作入口，words/tranco作阴性对照，cost/adaptive与F6动态矩阵后移。')
        replaced += 1
    else:
        out.append(line)
assert replaced == 1, replaced
readme.write_text('\n'.join(out), encoding='utf-8')

plan = pathlib.Path('/mnt/d/StringLearnedIndex_Workspace/docs/LONG_TERM_BM_PLAN_20261018.md')
text = plan.read_text(encoding='utf-8')
anchor = '预测块族累计两次入口失败（括号→payload），按本计划停止该方向；下一入口为cost/adaptive planner独立消融，\n或按阶段3先审计真实数据失辨分布。'
assert anchor in text
text = text.replace(anchor,
    '预测块族累计两次入口失败（括号→payload），按本计划停止该方向。\n'
    '阶段3失辨审计已完成，见[四格窗口/簇/回退](../07_Eval_Integration/RESULTS_BM_ENCODING_AUDIT_20261019.md)：'
    '窗口均值17–26块、覆盖98.2%–98.6%、缺席窗外1.3%–1.9%；同坐标簇仅URL显著（44.3%，簇内额外前缀中位11字节），'
    '短语25.2%但中位仅1字节，words/tranco<1%。据此登记下一候选为**URL类键的一层簇内局部前缀细分**，'
    'words/tranco作阴性对照、短语作低收益对照；cost/adaptive仍单列，不与编码细分同轮启用。')
plan.write_text(text, encoding='utf-8')
print('docs updated')

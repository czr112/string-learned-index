# ⚠️ 已不可运行（2026-09-18 核实）。
#
# 本脚本生成 00_Overview/StringLearnedIndex_Project_Overview_and_Guide.{md,docx}，
# 但：① 环境里没有 python-docx（第 3 行 import 即失败）；② 它依赖的 output/ 目录
# 已不存在；③ 脚本内的 sections 内容停留在 E8 时代。
#
# 因此 00_Overview/ 下的 **.md 自 2026-09-18 起改为手工维护，以 .md 为准**；
# docx 是 2026-09-14 的旧快照，与 .md 不一致。若要恢复脚本生成，需先装回
# python-docx 并把 sections 补到当前轮次——不要直接跑本脚本覆盖 .md。
from pathlib import Path
import re
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'output';OUT.mkdir(exist_ok=True)
TITLE='字符串学习型索引课题总览与资料导航'
# 2026-09-14：资料包目录与文件名改为英文，标题仍为中文；输出文件名走 OUTNAME。
OUTNAME='StringLearnedIndex_Project_Overview_and_Guide'
sections=[
('1 任务定位与当前结论',[
('p','这份总览将课题文档、论文梳理、实现方案和三轮自主验证连起来，帮助你快速判断自己要做什么、已经做到哪里，以及每个结论对应哪份文件。整理截至 2026 年 9 月 11 日，详细推导、论文入口和全部历史结果保留在随附完整报告中。'),
('p','你负责的核心任务是实现内存索引结构：承接分层字符串模型的位置预测，把预测结果变成实际数据布局和查找路径，并在增删改、重排和后续并发接入时维持正确性。这里沿用原课题中“单独的索引部分”的任务定位；它直接落实第二项创新，也需要与字符串模型负责人配合。'),
('p','原文两条主线分别是共享前缀压缩与差异化后缀学习，以及预测驱动的数据重排与预取。当前已做成可运行的单线程原型，完成三轮实验；但尚未达到可直接宣称高性能、低空间、并发完备和创新成立的最终状态。'),
('table',(['任务内容','当前状态'],[
['共享前缀与后缀模型接口','已实现区域前缀及三类参考模型；不是项目最终模型'],
['预测分组与精确查找','已实现实际内存块、指纹过滤、完整键比较和溢出处理'],
['动态更新与局部重建','已有单线程增删改查、扫描、分裂和同步重建'],
['提前停止与预取','已实现并对照；预取尚未显示稳定优势'],
['配置选择与强基线','已实现离线选择和原生 ART 对照；当前仍慢于 ART'],
['并发与在线调参','尚未实现并验证；不能写成已完成能力']
])),
('p','最新判断：最终策略在三组数据上比默认配置快约 14%–30%，但都未胜过本轮原生 ART。跨区域成本测量也没有稳定改善选型。它们是有证据边界的工程进展，不是已经完成的论文创新证明。')
]),
('2 基础知识与论文阅读入口',[
('p','先掌握四件事：索引是帮助找到记录的辅助结构；有序索引还要支持区间扫描；学习模型只能预测位置，最终仍须精确核对键；缓存友好意味着减少真实内存访问，不能只看模型计算量。完整报告第 2 节对应课件中的必要基础，第 3 节汇总 24 项文献或工作及入口。'),
('table',(['阅读阶段','论文或工作','要解决的问题'],[
['先理解结构','ART、HOT','路径压缩、节点组织、树高和字符串比较'],
['理解学习索引','RMI、PGM','预测位置之后怎样纠错与保证查找正确'],
['理解动态维护','ALEX、LIPP','插入空间、预测冲突、重建与结构调整'],
['精读直接参照','LITS、LIT+／LIT-MT','字符串模型怎样和布局协同；已有优化边界'],
['理解压缩与比较','SIndex、RSS、HOPE','公共前缀、后缀比较和保序压缩的代价'],
['核对布局相关工作','CARMI、Bigtable 学习索引、LINE','预测块、按模型布局、预取和维护开销'],
['设计可信实验','GRE、TLI、RoBin','公平基线、更新负载、分布变化与退化'],
['按需补读','DILI、SALI、LIVAK、LISK 等','分布驱动布局、节点演化及变长键近邻工作']
])),
('p','推荐按 ART → RMI／PGM → ALEX／LIPP → LITS → CARMI／LINE → GRE／TLI 的顺序进入主线。课件中完美哈希与 FairHash 等内容作为背景补充，不必与 LITS 投入相同的精读时间。'),
('p','精读时固定回答五个问题：键在哪里存？模型预测什么？预测不准怎么办？插入后怎样保持一致？收益是否抵得过额外空间与维护？完整报告第 4–5 节已将这些问题对应到论文和源码。'),
('p','阅读深度有区别。R15、LIVAK、LISK 等仍有摘要或出版信息级核对项；R18 的期刊记录与仓库说明也不能直接视为全文技术逐项对应。所有状态沿用原报告，不能把 24 项都写成已全文精读。原文与代码链接集中在资料包的“文献清单与阅读状态.md”。')
]),
('3 原型结构与三轮实现进展',[
('p','一次查找先按字典序定位区域，验证该区域的共享前缀，再用后缀模型预测数据组。组号一旦确定，就可以停止继续细化模型区间。随后利用指纹筛选候选，最终完整比较字符串；放不进主块的记录由所属组的溢出区保存。预测不代替精确核对。'),
('p','当前主块按 64 字节对齐，块头占 64 字节，固定 16 个条目各占 16 字节，其余空间存后缀。512、1024、2048 字节块对应 192、704、1728 字节后缀空间。因此“条目槽不足”和“后缀字节不足”是两个不同问题。'),
('table',(['轮次与目录','新增实现','关键反馈'],[
['第一轮\nvalidation_20260906','预测分组、整数区间停止、CRC 指纹、块头预取、更新与同步重建','可以保证已测输入的正确性；少算模型不等于同比例提速'],
['第二轮 E3\nvalidation_e3_20260906','三类模型与三种块宽组合、异质区域、容量预算选择、有序叶回退','溢出原因不同；局部暖访问成本会误判整体排序'],
['第三轮 E4\nvalidation_e4_20260911','跨区域测量、独立验证选择、同配置去重、原生 ART 适配','验证可回退统一配置；复杂联合选择仍非必胜']
])),
('p','三轮名称按实际工作次序排列，目录中的 E3、E4 是已有实验标识，不表示少交了一轮。第二、三轮使用同一存储实现；E4 通过相邻的 E3 目录引用代码，两者相对位置不能随意改变。'),
('p','目前查询、插入、更新、删除、扫描、区域重配置等接口已有实现。同步重建会暂停当前操作；它不是后台重建，也不是并发安全发布。硬件 CRC 已使用，SIMD 批量标签比较尚未实现。预取开关已保留，后两轮主要配置选择实验关闭预取。')
]),
('4 已验证的结果与适用边界',[
('table',(['验证范围','已执行的检查'],[
['第一轮','63000 次随机混合操作，9065 次扫描检查；另有 12000 次动态操作'],
['第二轮','70000 次混合操作，10618 次扫描检查，以及模型和容量守恒核对'],
['第三轮','420 条正式批次计时，22020096 次测试查询逐条核对；新增选择器和 ART 检查']
])),
('p','第三轮结果如下，单位 ns/次，越低越好。每组使用 3 个查询种子、每种子 5 轮，表中是批次平均耗时的中位数，不是单请求 P99。'),
('table',(['方法','重复前缀','混合字符串','百万域名'],[
['默认配置','806.88','696.66','756.32'],
['验证最佳统一配置','636.01','601.45','650.05'],
['验证最终选择','564.53','537.21','650.05'],
['原生 armon/libart','509.02','376.47','365.71']
])),
('p','最终选择相对默认减少约 30.0%、22.9%、14.1% 的查询时间，但相对 ART 分别慢约 10.9%、42.7%、77.7%。域名最终选择统一 position16／512；它不是异质联合配置赢过统一配置的证据。'),
('p','第三轮按键序号分开调参、验证和测试查询，验证后冻结配置再测试；命中率为 50%，未命中用可见字符后缀构造。前轮负载不同，绝对耗时不能跨轮直接比较。空间是原型容量估计，不是 RSS；ART 完整空间尚未按同一口径计量，不能声称它满足相同预算。'),
('p','LITS 原生发布构建返回值核对通过，但内存检查先后发现 PMSS 表上界、HPT 起始上下文与内嵌 HOT 对齐问题。保留两项边界补丁和失败日志，未列入正式性能对照。mixed 的不完整尝试也整体归档并排除；完整重跑使用原参数。'),
('p','所有结论限于已执行输入与单一共享主机。原型与 ART 的相关 ASan／UBSan 检查通过，LeakSanitizer 退出扫描因环境限制关闭；没有完成并发、内存泄漏专项或硬件缓存未命中率证明。')
]),
('5 创新判断与下一步实现顺序',[
('p','原文给出研究方向，论文贡献则要靠具体机制和证据支撑。目前不能把“按预测分块”“提前停止”“加入预取”“使用联合选择器”单独称为已证实的新方法；已有论文涉及其中部分思想，且当前实验仍存在反例。'),
('p','已经得到的有用证据是：域名默认溢出主要受条目槽限制，重复前缀长键主要受后缀字节限制。统一增加块大小不一定有效，应把模型分组、条目目录和后缀空间作为相互影响的资源分配问题。'),
('table',(['顺序','下一步具体实现','验收重点'],[
['1','把固定 16 槽改成可配置的 8／16／32 槽，并统一修改查找、插入、重建的布局描述','精确结果、容量守恒、槽与字节溢出、真实端到端速度'],
['2','预先规定接近最快时优先更小、更简单的选型规则，并减少同时驻留候选','独立验证与测试、峰值内存、构建及选型摊平成本'],
['3','分离外层路由、模型与溢出开销，再评估预取和布局改动','必要消融和原生 ART 对照；不以模型字节数代替总耗时'],
['4','继续修复与核对 LITS；与并发负责人约定模型和布局的整体发布','基线检查通过、读写一致性、重试与对象安全回收']
])),
('p','这些是下一轮待实施项，不能与已经交付的代码混写。本轮完整报告第 17 节还记录了选型成本：相对直接建默认索引，约多花 6.44、6.31、12.95 秒；在当前静态负载下需要大量后续查询才能摊平。'),
('p','如果向导师汇报，可以准确表述为：已完成字符串学习型索引的单线程结构原型与三轮自主验证，建立了从后缀预测到物理分组、动态维护和配置选择的接口，并用强基线暴露了现有方法的性能与空间边界。尚未完成最终性能优势、并发系统和新颖性证明。')
]),
('6 文件导航与复现方法',[
('p','完整资料包中，总览用于快速掌握进度，详细报告用于阅读理论与历史推导，实验目录用于核对证据和运行代码。原始课题文档与课件原样保留。'),
('table',(['位置','内容与阅读入口'],[
['00_Overview','本总览的 Word 和 Markdown 版本'],
['01_Research_Report','完整 17 节研究报告，以及单独提取的 24 项文献清单'],
['02_Code_and_Experiments','原验证包的完整内容；E4 的 FINDINGS.md 是最新实验结论入口'],
['03_Source_Materials','上传的学习型索引课题 DOCX 与 52 页 PPTX'],
['04_Consolidation_and_Verification','文件清单、文件摘要和总览生成脚本']
])),
('p','建议先读本总览，再读完整报告第 1–5 节掌握任务、基础与论文；第 14 节解释具体实现，第 15–17 节记录三轮验证。开始改代码前，读 E4 的 README.md、PROTOCOL.md 和 NATIVE_STATUS.md。'),
('p','在解压后的“02_Code_and_Experiments”目录执行以下命令。第一条编译并检查，第二条重跑主实验；完整说明以 E4 README 为准。'),
('code','python validation_e4_20260911/reproduce.py\npython validation_e4_20260911/reproduce.py --full'),
('p','复现需要 Linux x86-64、GCC/G++、Python 3、taskset 及兼容的 CPU 指令。第三方源码和 Tranco 固定快照由脚本下载并核对摘要，初次运行需要网络。包内没有论文全文集合、原始百万域名 CSV、第三方完整源码或编译后的大体积二进制；论文入口、固定版本和下载脚本已经提供。'),
('p','代码包保留前两轮材料和第三轮失败记录；请在副本中实验，不要把不完整尝试或 smoke 连通性测试纳入正式结果。各轮自己的文件清单和摘要继续有效，总包另附完整路径清单。本文是截至 2026 年 9 月 11 日的整理快照，后续进展应继续记录新日期和验证范围。')
])]

doc=Document();sec=doc.sections[0];sec.page_width=Inches(8.5);sec.page_height=Inches(11)
sec.top_margin=Inches(.68);sec.bottom_margin=Inches(.65);sec.left_margin=sec.right_margin=Inches(.72)
for name in ['Normal','Title','Subtitle','Heading 1','Heading 2']:
 s=doc.styles[name];s.font.name='Noto Sans CJK SC';s.font.color.rgb=RGBColor(0,0,0)
 s.element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'Noto Sans CJK SC')
 s.paragraph_format.space_after=Pt(7)
doc.styles['Normal'].font.size=Pt(10.5);doc.styles['Normal'].paragraph_format.line_spacing=1.22
doc.styles['Title'].font.size=Pt(21);doc.styles['Title'].paragraph_format.space_after=Pt(8)
doc.styles['Heading 1'].font.size=Pt(16);doc.styles['Heading 1'].paragraph_format.space_before=Pt(0)
doc.styles['Heading 1'].paragraph_format.space_after=Pt(10)
doc.core_properties.title=TITLE;doc.core_properties.subject='第二项任务的方案 实现 证据和完整资料导航'
doc.core_properties.author='';doc.core_properties.keywords='字符串索引 学习型索引 项目总览'
foot=sec.footer.paragraphs[0];foot.alignment=WD_ALIGN_PARAGRAPH.CENTER
field=OxmlElement('w:fldSimple');field.set(qn('w:instr'),'PAGE');foot._p.append(field)

def table(headers,rows):
 t=doc.add_table(rows=1,cols=len(headers));t.alignment=WD_TABLE_ALIGNMENT.CENTER;t.autofit=False
 widths={2:[2.0,5.06],3:[1.45,2.8,2.81],4:[2.5,1.52,1.52,1.52]}[len(headers)]
 for col,w in zip(t.columns,widths):col.width=Inches(w)
 for row,vals in [(t.rows[0],headers)]+[(t.add_row(),r) for r in rows]:
  for i,(cell,v) in enumerate(zip(row.cells,vals)):
   cell.width=Inches(widths[i]);cell.vertical_alignment=WD_CELL_VERTICAL_ALIGNMENT.CENTER
   p=cell.paragraphs[0];p.paragraph_format.space_after=Pt(4);p.paragraph_format.space_before=Pt(4);p.paragraph_format.line_spacing=1.14
   p.add_run(v).font.size=Pt(9.4)
   if len(headers)==4 and i>0:p.alignment=WD_ALIGN_PARAGRAPH.CENTER
   tcPr=cell._tc.get_or_add_tcPr();m=OxmlElement('w:tcMar')
   for edge in ['top','left','bottom','right']:
    e=OxmlElement('w:'+edge);e.set(qn('w:w'),'85');e.set(qn('w:type'),'dxa');m.append(e)
   tcPr.append(m)
  trPr=row._tr.get_or_add_trPr();trPr.append(OxmlElement('w:cantSplit'))
 for cell in t.rows[0].cells:
  sh=OxmlElement('w:shd');sh.set(qn('w:fill'),'DCE6F1');cell._tc.get_or_add_tcPr().append(sh)
  for run in cell.paragraphs[0].runs:run.bold=True
 t.rows[0]._tr.get_or_add_trPr().append(OxmlElement('w:tblHeader'))
 borders=OxmlElement('w:tblBorders')
 for edge in ['top','left','bottom','right','insideH','insideV']:
  el=OxmlElement('w:'+edge);el.set(qn('w:val'),'single');el.set(qn('w:sz'),'5');el.set(qn('w:color'),'D9D9D9');borders.append(el)
 t._tbl.tblPr.append(borders)
 doc.add_paragraph().paragraph_format.space_after=Pt(0)

md=['# '+TITLE,'','整理日期：2026-09-11。','']
for i,(heading,blocks) in enumerate(sections):
 if i==0:
  doc.add_paragraph(TITLE,'Title');doc.add_paragraph('第二项任务的方案  实现  证据与文件入口\n整理日期 2026 年 9 月 11 日','Subtitle')
 doc.add_heading(heading,1);md+=['## '+heading,'']
 for typ,value in blocks:
  if typ=='table':
   h,rows=value;table(h,rows);md+=['| '+' | '.join(h)+' |','| '+' | '.join(['---']*len(h))+' |']+['| '+' | '.join(x.replace('\n',' / ') for x in r)+' |' for r in rows]+['']
  elif typ=='code':
   p=doc.add_paragraph();p.paragraph_format.space_after=Pt(8)
   run=p.add_run(value);run.font.name='DejaVu Sans Mono';run.font.size=Pt(9)
   md+=['```bash',value,'```','']
  else:doc.add_paragraph(value);md+=[value,'']
doc.save(OUT/(OUTNAME+'.docx'))
(OUT/(OUTNAME+'.md')).write_text('\n'.join(md),encoding='utf-8')
print('Created',OUT/(OUTNAME+'.docx'))

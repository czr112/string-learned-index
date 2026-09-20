# ⚠️ 已不可运行（2026-09-18 核实）。
#
# 本脚本打包并生成根 README.md 与两份 MANIFEST.sha256，但它依赖的
# consolidation/（STAGE 与 package/）、upload/、output/ 三个目录都已不存在。
#
# 那两份清单**仍然 185/185 全部有效**，且覆盖范围就是 2026-09-11 那个交付包
# （02_Code_and_Experiments/ 只到 E4）——它们是**冻结快照**，不是损坏。
# 覆盖范围说明见根 README「清单覆盖范围」；当前完整工作区的带日期快照见
# 04_Consolidation_and_Verification/MANIFEST_workspace_20260918.sha256。
from pathlib import Path
import hashlib, json, shutil, zipfile

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'output'
STAGE=ROOT/'consolidation'/'final_package'
STAGE.mkdir(parents=True, exist_ok=True)
# 2026-09-14：目录与文件名统一改为英文；原中文名对照见 README「目录改名记录」。
OVERVIEW='StringLearnedIndex_Project_Overview_and_Guide'
for name in ['00_Overview','01_Research_Report','02_Code_and_Experiments','03_Source_Materials','04_Consolidation_and_Verification','05_Historical_Versions']:
    (STAGE/name).mkdir(parents=True, exist_ok=True)

def copy(src, dst):
    dst=STAGE/dst
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)

copy(OUT/(OVERVIEW+'.docx'),'00_Overview/'+OVERVIEW+'.docx')
copy(OUT/(OVERVIEW+'.md'),'00_Overview/'+OVERVIEW+'.md')
copy(OUT/'StringLearnedIndex_Literature_Review_and_Task2_Plan.md','01_Research_Report/StringLearnedIndex_Literature_Review_and_Task2_Plan.md')
copy(ROOT/'consolidation/package/01_Research_Report/Bibliography_and_Reading_Status.md','01_Research_Report/Bibliography_and_Reading_Status.md')

validation_zip=OUT/'StringLearnedIndex_SelfValidation_20260906.zip'
with zipfile.ZipFile(validation_zip) as source:
    for item in source.infolist():
        if item.is_dir():
            continue
        if item.filename.startswith('/') or '..' in Path(item.filename).parts:
            raise RuntimeError(f'unsafe validation entry: {item.filename}')
        target=STAGE/'02_Code_and_Experiments'/item.filename
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read(item))
copy(validation_zip,'05_Historical_Versions/StringLearnedIndex_SelfValidation_20260906.zip')

for name in ['Learned_Index(1).docx','mm_part1_intro_1(1).pptx']:
    copy(ROOT/'upload'/name,'03_Source_Materials/'+name)
copy(ROOT/'consolidation/build_overview.py','04_Consolidation_and_Verification/build_overview.py')
copy(ROOT/'consolidation/build_package.py','04_Consolidation_and_Verification/build_package.py')

readme='''# 字符串学习型索引课题总资料包

整理日期：2026-09-11。目录与文件名英文化：2026-09-14。

这是目前反馈内容的总资料包。建议先看 `00_Overview/StringLearnedIndex_Project_Overview_and_Guide.docx`，再读 `01_Research_Report/StringLearnedIndex_Literature_Review_and_Task2_Plan.md`。代码与实验在 `02_Code_and_Experiments/`，其中 `validation_e8_20260913/RESULTS_E8.md` 是最新实验结论入口；原始课题文档和课件原样保存在 `03_Source_Materials/`。

## 内容结构

- `00_Overview`：面向快速理解的 Word 总览和 Markdown 版本。
- `01_Research_Report`：完整 17 节研究报告、论文入口和阅读状态。
- `02_Code_and_Experiments`：第一至第三轮的代码、数据记录、协议、日志和复现脚本，以及 2026-09-13 的 E5–E8 四轮验证。E4 通过相邻的 E3 目录引用底层存储代码，相对位置不要改变。
- `03_Source_Materials`：上传的《Learned_Index(1).docx》和《mm_part1_intro_1(1).pptx》。
- `04_Consolidation_and_Verification`：整理脚本与总包文件摘要。
- `05_Historical_Versions`：上一轮验证 ZIP 的原始字节副本，便于追溯。

## 目录改名记录（2026-09-14）

目录与文件名由中文改为英文，**文件内容未改动**（345 个文件逐一比对 SHA-256 与改名前的快照完全一致）。对照如下：

| 原名称 | 现名称 |
| --- | --- |
| `字符串学习型索引_工作区/` | `StringLearnedIndex_Workspace/` |
| `00_总览/` | `00_Overview/` |
| `01_研究报告/` | `01_Research_Report/` |
| `02_代码与实验/` | `02_Code_and_Experiments/` |
| `03_原始材料/` | `03_Source_Materials/` |
| `04_整理与校验/` | `04_Consolidation_and_Verification/` |
| `05_历史版本/` | `05_Historical_Versions/` |
| `00_总览/字符串学习型索引课题总览与资料导航.md/.docx` | `00_Overview/StringLearnedIndex_Project_Overview_and_Guide.md/.docx` |
| `01_研究报告/字符串学习型索引_论文梳理与第二项任务方案.md` | `01_Research_Report/StringLearnedIndex_Literature_Review_and_Task2_Plan.md` |
| `01_研究报告/字符串学习型索引_三轮验证代码结构讲解.md` | `01_Research_Report/StringLearnedIndex_Code_Structure_Walkthrough.md` |
| `01_研究报告/文献清单与阅读状态.md` | `01_Research_Report/Bibliography_and_Reading_Status.md` |
| `03_原始材料/学习型索引(1).docx` | `03_Source_Materials/Learned_Index(1).docx` |
| `05_历史版本/字符串学习型索引_自主验证_20260906.zip` | `05_Historical_Versions/StringLearnedIndex_SelfValidation_20260906.zip` |

两点必须注意：

- `02_Code_and_Experiments/validation_e5_20260913` 至 `validation_e8_20260913` 的 `logs/build_*.json` 与 `build/*.built.json` 里记录的是**旧的绝对路径** `D:\\字符串学习型索引_工作区\\...`。按记录纪律这些文件原样保留，未回改；核对当期构建时请按上表换算。
- 本轮只改名称，未重新运行任何实验。总包 `MANIFEST.sha256` 已按新路径重生成，覆盖范围与 2026-09-11 版本一致。

## 当前总判断

原型已经实现共享前缀、差异化参考模型、预测分组、精确查找、动态更新、局部重建、离线配置选择和 ART 对照。三组正式测试中，验证后的策略比默认配置快约 14%–30%，但仍慢于本轮原生 armon/libart；跨区域代理没有稳定改善策略排序。LITS 接入已保留诊断与失败证据，因内存安全检查未通过而未纳入正式计时。2026-09-13 的 E5–E8 四轮进一步定位到：外层路由占端到端 15%–26%，溢出进入率高达 78%–97%，统一容量与实际组需求明显错配（等预算下重分配可减少溢出 21.9%–62.1%）。并发、在线调参和最终论文新颖性仍未完成验证。

## 复现

在 `02_Code_and_Experiments/validation_e4_20260911/` 中执行：

```bash
python reproduce.py
python reproduce.py --full
```

完整说明见 E4 的 `README.md`、`PROTOCOL.md` 和 `NATIVE_STATUS.md`。第三方源码和 Tranco 固定快照由脚本下载并校验；包内不重复放置原始百万域名 CSV，也不包含编译后二进制。
'''
(STAGE/'README.md').write_text(readme,encoding='utf-8')

paths=[]
for p in sorted(STAGE.rglob('*')):
    if p.is_file() and p.name != 'MANIFEST.sha256':
        paths.append(p)
manifest=''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(STAGE)}\n' for p in paths)
(STAGE/'MANIFEST.sha256').write_text(manifest,encoding='utf-8')

zip_path=OUT/'StringLearnedIndex_Full_Package_20260911.zip'
with zipfile.ZipFile(zip_path,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for p in sorted(STAGE.rglob('*')):
        if p.is_file():
            z.write(p,p.relative_to(STAGE))
with zipfile.ZipFile(zip_path) as z:
    if z.testzip() is not None:
        raise RuntimeError('zip integrity failure')
    for line in z.read('MANIFEST.sha256').decode().splitlines():
        digest,name=line.split('  ',1)
        if hashlib.sha256(z.read(name)).hexdigest()!=digest:
            raise RuntimeError(f'manifest mismatch: {name}')
print(json.dumps({'zip':str(zip_path),'entries':len(zipfile.ZipFile(zip_path).namelist()),'bytes':zip_path.stat().st_size,'manifest_files':len(paths)},ensure_ascii=False))

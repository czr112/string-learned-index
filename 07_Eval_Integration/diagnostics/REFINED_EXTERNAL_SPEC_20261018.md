# E1正式只读集成与ART/HOT复测（计时前登记）

起点601d4d7。D1四份实现头文件保持原样；新独立our_static_refined包，生产默认不改。
manifest/CMake绑定完整实现bundle和适配器身份，先做统一发现smoke、共用接口验收和四份完整O2/ASan/UBSan门。
各构建分别验语义/覆盖/范围/内存；不跨优化构建强制Work全等，不为性能改算法/参数/编译ISA。

从ART/HOT固定源码自行新建CMake对象，不复用历史.o：libart@301046804af165269e37da6725f5a4aec9ecc881（校验c/h字节），
HOT@96bf6fb7103b27e50e16a6026db8974c090ee84a（干净checkout及实际编译依赖）。
编译g++/gcc13.3.0，O3/C++17或C99/x86-64-v3，NDEBUG不定义；HOT保留既有私有allocator选项。
完整性能入口通过同一v1虚接口，正式工厂单独翻译单元并链接实际CMake库，原型使用冻结D1统一包装。

六标签：prototype、prototype_AA（同实例）、integrated、integrated_AA（同实例）、ART、HOT。
八格固定为tranco full/64k、mixed full、skew full各10k和100k S3冻结请求；5进程×6 Williams行(0,1,5,2,4,3平移)。
位置/有向前驱平衡，绑可用CPU；每臂即时200k次完整预热和200k次计时，均先验全库键和所有请求的presence/value。
构建4个独立实例，顺序按process模4轮转，记录每次完整构建及内存；构建时间是本轮描述性数据，峰值仍待后续独立核查。

同重复比值每进程取中位，再取5进程中位。F取prototype/integrated双同实例A/A对称带较大者。
integrated/prototype>1.05、≥4/5偏慢且超F，判清晰集成回归；不追加重复挽救。
相对外部基线的清晰差异需5%、≥4/5同向并超F；表列全部直接比值，未分辨不证明相等。
工程目标仍为全部必测格≤ART的1.10倍；进阶至少两格≤ART且含tranco/64k或mixed/10k。
目标失败不改目标格或平均掩盖单格，只保留明确场景；无集成回归也不代表原S3的7.8%已解释。

本轮E1交付正式适配包、发现/完整验收、最终接口八格外部比值与是否进入有限泛化的决定。
无放回遍读、命中端点、首次遍读、控制数据、百万规模及ART完整分配/峰值口径属于后续E步骤，不能声称已完成。
默认仍Context16，不做LITS优化或动态功能。性能/集成门失败可收口为场景候选，不立即进入F。

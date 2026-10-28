# F5预登记：联合导航、dirty维护及同步换根

本轮将F4的flat root + single-page完整点查接到F4M按需维护版本。
这是联合工程假设，不修改F4的selected_single_mechanism=null，也不由跨轮比值推导收益。
F3/F4/F4M源码、输入、结果冻结；生产默认Context16不变。

## 语义与发布

叶页直接使用冻结route_attribution_page.hpp；模型、CRC、Block16、字节比较及固定fence不变。
map持有页，根持有自有separator和借用页指针，点查由根选页并调用find_single。
槽内插入、值更新和删除不换根；单页替换、分裂及相邻合并必须先准备所有新页和下一版根，
再执行无分配的map节点转移和noexcept根交换。仅提供单线程异常安全，不声称并发发布。
prepare前/完成后注入bad_alloc，覆盖split、full-slot替换、compact和两页merge。
失败保留旧页、根与dirty成员；已成功准备但未发布的根也计入复制账与峰值。
verify先核对根与持有map的separator/页指针一一对应，再解引用并验证点查/定位契约。

保留F4M所有回归，并用独立std::map和冻结F4M对照随机/真实写入、完整scan、全删至单空页、
任意字节键和65,536个两字节查询。普通O2和ASan/UBSan四组必须全过且输出一致。
空间必要门：三个真实语料初始完整自计bytes / 同数据D1 <=1.10；未过不计时。

根重建为O(页数)，保留map所有权，不能宣称整个写路径局部化。
记录root_prepare_attempts、root_prepared、root_publications（含初始化）、root_entries_copied、
root_key_bytes_copied、largest_root_entries、root_bytes和accounted_peak_bytes。
复制项/键字节计成功准备的根（包括中止发布）；中途真实分配失败的部分复制不提供精确计数。
自计包括旧/新根、旧/新页、已知行缓冲；排除map/set节点链接、分配器元数据及页构建器内部scratch，
因此不是完整分配峰值。动态矩阵必须另补完整分配峰值，不能据此采用。

## 最终完整点查必要门（语义/空间门之后）

输入使用E2身份中的tranco64_norepl、mixed、skew，分别clean/dirty六格。
dirty固定1024次既有键值update，seed=0xf2a20261018；D1从同一当前std::map重建。
保持F3相同查询：每10条追加~f2/miss且证明缺失，其他取当前值；每条执行完整find。
四臂live/live_AA/D1/D1_AA，AA为同一实例。每格5个独立进程，各8轮；
Williams顺序[0,1,3,2]，按(repeat+process)%4旋转；每轮共用相同shuffle(seed=0x12345f2+repeat)。
O3、march=x86-64-v3、单CPU固定，先全部六格普通/ASan完整查询与证书检查，再开始计时。
总计6×5×8×4=960条timing；不追加重复、不按结果选顺序或参数。

每进程取8个配对live/D1比值中位数，再取5进程中位数。双方AA带取各进程AA比值对称最大值。
live/D1中位数>1.15、至少4/5进程慢于D1且超双方AA带，则关闭本联合布局的通用采用。
若中位数>1.15但被AA遮蔽，标为unresolved，不能冒充通过。
六格中位数均<=1.15才可继续后续动态吞吐/维护矩阵；这只是必要读门，不是采用证明。
不得把F4的局部归因计时替代本轮对同快照D1的直接测量。

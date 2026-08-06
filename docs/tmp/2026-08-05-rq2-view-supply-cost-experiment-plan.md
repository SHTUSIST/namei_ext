# Experiment Plan: RQ2 Per-View Supply Cost Versus Mount-Based Views

## Research Question

- 论文里写的研究问题(逐字保留原意):相对于功能等价的 FUSE 实现,把可编程策略放到 VFS 名字解析路径上的代价是多少。
- 本实验测的具体不确定性:**当同一台机器上的视图数量增长时,本机制相对于基于挂载的做法,每一份视图的供给成本差多少。** 这一点此前没有被任何实验测过。
- 为什么这个答案重要:论文目前唯一独有的主张是"同一条路径按任务给出不同答案、而且可以在运行中改",但支撑这个主张的成本证据是空的。相对 FUSE 的既有数字(缓存命中查找 1.052–1.088、目录枚举 2.20–3.66 倍)回答的是另一个问题——单次操作的代价,而不是每份视图的供给成本。

## Paper-Value Admission

- 计划角色:核心(这是当前最缺的一格)。
- 这个实验能解锁的最大论文主张:在一个挂载命名空间之内提供大量按任务分叉的路径视图时,每视图成本随视图数量的增长关系优于每视图一次挂载的做法。
- 它针对的最强审稿人反驳:「我为了写隔离反正要用 overlayfs,而 overlayfs 顺便就把视图也给我了,我为什么还需要你?」这个问题目前答不上来。

- 它不是重复既有工作的理由:既有的 FUSE 对比测的是单次操作延迟与吞吐,不是每视图的供给成本;FxMark 与目录枚举矩阵都固定在一份视图上跑。
- 结果为正时的论文动作:新增一张随视图数量变化的曲线图,放进 RQ2,并把主张收窄成"每视图供给成本",不提速度。
- 结果为负或不显著时的论文动作:**如实报告,并把"每视图更便宜"这条主张从论文里撤掉**,只保留"可以在运行中改答案"这一条能力主张,同时明写在成本上没有优势。不许改阈值。

## Expected And Alternative Outcomes

- 当前预期:本机制的每视图成本近似常数;挂载路线的成本随视图数量线性增长,并且在挂载操作的全局锁上出现排队,视图数量越大排队越明显。
- 替代结果一(差距不显著):两条曲线的配对比值置信区间跨过 1。这说明在本实验覆盖的视图数量范围内,挂载路线的开销还不构成问题,论文不能主张成本优势,只能保留可在运行中改答案这条能力主张。
- 替代结果二(只在极大视图数下才显著):在 1 到 100 档没有差别,只在 1000 或 10000 档出现差距。这时论文必须把主张限定在那个数量区间内,并同时说明目前没有具名的生产部署跑在那个区间。
- 替代结果三(本机制反而更差):某个指标上挂载路线更省。这是可以发表的负结果,直接写进论文,并说明代价来自哪一部分(例如策略程序的每次查找开销随视图数量增长)。

## Published Precedent And Real Assets

- 第三方已发表的相关测量:TClone(arXiv 2605.17320,标题 `TClone: Low-Latency Forking of Live GUI Environments for Computer-Use Agents`)第 5.3.3 节测了 overlayfs 随分支深度的表现,原句是 `Only a cold shared read (read_ro) walks the chain, growing linearly with no per-layer compounding from about 1.3 ms to about 5.7 ms at depth 50`,以及 `Thus, OverlayFS inflates every lower-layer read as the union deepens, unsuitable for read-heavy and snapshot frequent CUA branching.`
- 页缓存重复的依据:LWN 2022-05-24,Jake Edge,原句是 `When two files share an extent, their inodes point at the same data blocks on the disk, though they seem to be completely independent files.` 与 `When those files are read, each gets copied separately into the page cache.`
- 我方此前的实测数字(**未在本轮重跑,需要在本实验里重新测一遍**):每条挂载表项静态占 35–47.5 KB;复制一次挂载命名空间 47.8 / 184.7 / 794.3 微秒;挂载操作在压力下等 1.27 秒、空载 5.9 毫秒。
- 复用的现成资产:本仓库既有的改内核 KVM 启动流程、结果目录约定与统计分析脚本;既有策略程序与目标注册接口。

## Comparison

四个条件,全部在同一台改过内核的虚拟机里跑:

1. **每视图一个挂载命名空间加绑定挂载。这是主对照**,因为它和本机制做的是同一件事:决定同一条路径在不同任务里指向哪棵树,不负责写隔离。
2. **每视图一次 overlayfs 挂载。** 它比本机制多给了写隔离,比较时必须声明这一点。
3. **每视图一次块级克隆(btrfs 子卷快照)。** 同样多给了写隔离。
4. **本机制:每视图几条策略规则。**

条件 2 和条件 3 不是同功能对照,它们多做了写隔离这一整件事。报告时必须把这一点写在图注里,否则就是不公平比较。判决只以条件 1 与条件 4 的对比为准;条件 2 和 3 的曲线作为参考,说明"顺便把视图也给了你"的那两条路线各自要付多少。

信息与调优上的公平:四个条件用同一批底层目录树、同一份视图内容清单、同一套虚拟机资源、同样的预热次数与测量次数;条件顺序按启动轮换。

**2026-08-05 补充:建议增加第五个条件——按 DeltaBox 的做法给 overlayfs 加上运行中重排层栈的能力之后的对照。** DeltaBox 已发表的做法是用 XFS 加 reflink 作底,配一个改过的 overlayfs 内核模块,通过一个自定义的 ioctl(让用户态程序向内核下达特定控制命令的接口)在不卸载的情况下重排层栈;它的动机原句我们亲手抓 arXiv 正文第 4.1 节核实过:`Standard Linux overlayfs fixes its layer stack at mount time; reconfiguring it requires an umount/mount cycle, impossible while the agent holds open files and untenable at the checkpoint rates MCTS demands.`

**如果这一条件暂时不实现,必须在 `## Interpretation` 一节里写明**:本实验不能回答"给 overlayfs 加一个重排层栈的 ioctl 是否就够了",而这正是审稿人会问的问题;我们的答复只能建立在按任务与按挂载的粒度差别上(改层栈按挂载生效,同一挂载点上所有使用者一起换;我们的判定按任务生效,同一挂载点上不同任务可以同时看到不同的东西),而不是能力有无。

## Frozen Mechanism Configuration

正式跑之前必须冻结并写进结果元数据的东西:

- 内核提交号(改过的内核)与本仓库源码提交号。
- 虚拟机规格:vCPU 数量与绑核方式、内存大小、内核启动参数。
- 底层文件系统与创建参数:文件系统类型、mkfs 选项、挂载选项;条件 3 需要 btrfs,条件 1、2、4 使用哪种文件系统必须写明(见 Reproducibility Notes 第 1 条)。
- cgroup 层次:策略挂在哪一级、被测进程属于哪个 cgroup、旁观者进程属于哪个 cgroup。
- 策略程序:BPF 源码提交号与编译产物哈希;每视图写入的规则条数。
- **btrfs 子卷是否开启配额组必须写明并冻结**,因为官方文档承认开启配额组时快照规模化会有不可接受的延迟;两种设置下的数字不可混用。
- 视图内容清单:目录树的文件数、目录数、总字节数,以及每份视图与基线树的差异条数。
- **2026-08-05 补充:条件 2(每视图一次 overlayfs 挂载)必须显式开启 `metacopy` 与 `redirect_dir` 两个挂载选项,并把完整的挂载选项串记录进结果元数据。** 理由是不开这两个选项等于拿一个被人为削弱的 overlayfs 作对照,得出的数字没有说服力:YoloFS 论文里那条"把基线目录树镜像到上层太贵"的成本论证,正是因为没考虑这两个选项而不成立。内核官方文档原句(我们亲手核实过):`metacopy` 使得 `When the "metacopy" feature is enabled, overlayfs will only copy up metadata (as opposed to whole file), when a metadata specific operation like chown/chmod is performed.`,而且 `The data will be copied up later when file is opened for WRITE operation.`;`redirect_dir` 使得改目录名时 `the directory will be copied up (but not the contents). Then the "trusted.overlay.redirect" extended attribute is set to the path of the original location from the root of the overlay.` 两个选项的开关状态在整个矩阵内必须一致,不同设置下的数字不可混用。

## Workloads And Metrics

视图数量取 1、10、100、1000、10000 五档,每档四个条件都跑。

四个指标:

1. **每份视图占用的内核内存。** 用两种口径交叉验证:一是 slab 分配器的分项统计(内核给自己的数据结构分配内存的统计),二是建立视图前后的整机可用内存差值。两种口径的结果都要报,差得太多说明测量口径有问题,不能只挑一个报。
2. **建立一份视图的耗时。** 报中位数、p95、p99。
3. **切换一份视图的耗时。** 定义为:把某个任务的视图从 A 换成 B,并且被一个新产生的子进程观察到。**这一项对挂载路线要专门记录一件事:当该挂载点仍被进程占用时,卸载会失败(报设备忙)。** 所以挂载路线要分别报两种情形——"必须先让使用者退出"和"不必让使用者退出"——不能只报后者。
4. **全部视图同时读同一批内容时的总页缓存占用。** **这一项是本实验里最有区分度的一项**:块级克隆那一条预期会随视图数量线性增长(依据是 Published Precedent 一节里 LWN 的那两句),而挂载路线与本机制预期只占一份内容的量。

正确性检查:每个档位、每个条件都要验证视图确实生效——即每个任务在同一条路径上读到的确实是分配给它的那棵树,而不是共同的基线树。任何一档验证不过,该档作废,不作为统计结果。

## Planned Runs

| Run group | Role | Workload | System/method | Repetitions | Decision consequence |
|---|---|---|---|---:|---|
| main | 主对照 | 1/10/100/1000/10000 份视图 | 每视图一个挂载命名空间加绑定挂载 | 每档 ≥30 | 判决只以它与 namei_ext 的配对比值为准 |
| main | 被测机制 | 同上 | namei_ext,每视图几条策略规则 | 每档 ≥30 | 被测的一侧 |
| reference | 参考(多给了写隔离) | 同上 | 每视图一次 overlayfs 挂载 | 每档 ≥30 | 只作参考曲线,不参与判决,图注须声明 |
| reference | 参考(多给了写隔离) | 同上 | 每视图一次 btrfs 子卷快照 | 每档 ≥30 | 只作参考曲线,页缓存那一项预期线性增长 |
| oracle | 正确性对照 | 每档抽样任务 | 四个条件 | 每档每条件 1 次 | 视图未生效则该档作废,不作为统计结果 |

- 至少三次全新虚拟机启动;每个视图数量档位、每个条件的重复次数不少于 30 次(微基准口径)。
- bootstrap 重抽样一万次,随机种子写死并提交进仓库;报 95% 置信区间。
- **预先声明的判决规则**:某个指标上"本机制优于主对照(条件 1)"当且仅当配对比值的 95% 置信区间整个落在有利的一侧;置信区间跨过 1 判无定论;整个落在不利一侧判推翻。**不得事后调整阈值。**

## Execution

- 按仓库既有做法,用一个 Make 目标一键复现,不新增独立脚本。
- 正式矩阵之前先跑一次依赖联调:小规模走通四个条件的建立、切换、测量与清理全流程,确认 btrfs 子卷、cgroup、策略加载、页缓存清空都真的生效。
- 结果写进不可改写的结果目录,按运行编号分目录;失败或已完成的结果目录一律保留、不复用,修好协议之后用新的运行编号重跑。
- 正式结果须有独立复核报告:由分析程序从原始样本重新计算配对效应与置信区间,并给出 supported / contradicted / inconclusive 三选一的结论。
- 完成判据:三次启动全部完成;每档每条件的有效样本数达到预定次数;每档正确性对照全部通过;分析程序给出终局结论。

## Interpretation

这个实验**不能**得出的结论,必须在论文里写清楚:

- **不能得出本机制比 overlayfs 更好。** overlayfs 多给了写隔离,条件 2 与本机制不是同功能对照。
- **不能得出速度更快。** 这个实验不测单次操作延迟,也不测应用端到端时间;既有的相对 FUSE 的数字(缓存命中查找 1.052–1.088、目录枚举 2.20–3.66 倍)回答的是另一个问题。
- **不能推广到别的机器。** 结论限定在冻结配置里写明的那台虚拟机、那种底层文件系统与那份视图内容清单上。
- **2026-08-05 补充:只要 `## Comparison` 里那个第五条件没有实现,就不能回答"给 overlayfs 加一个重排层栈的 ioctl 是否就够了"。** 这是审稿人会问的问题,DeltaBox 已经把这条路实现出来并发表了。这种情况下我们的答复只能建立在按任务与按挂载的粒度差别上,而不是能力有无:改层栈按挂载生效,同一挂载点上的所有使用者一起换;我们的判定按任务生效,同一挂载点上不同任务可以同时看到不同的东西。这句限制要一并写进论文。

可以得出的结论只有一条:在这台机器、这个视图数量范围内,每份视图的供给成本随视图数量的增长关系,本机制与主对照相比是什么样。

## Reproducibility Notes

1. **底层文件系统若用 ext4,块级克隆那一条不可用。** ext4 没有实现块级克隆,而且 `cp` 的默认行为会**无声地退化成整份拷贝**——手册原句是 `By default or with --reflink=auto, cp will try a lightweight copy, where the data blocks are copied only when modified, falling back to a standard copy if this is not possible.` 所以必须专门检查块是否真的共享(例如比对文件占用的实际磁盘块与逻辑大小),否则会把一次整份拷贝误报成克隆成功。(**这两条由检索环节抓取,我们没有重抓原文,执行前请再核一次。**)
2. **btrfs 子卷快照必须在源已经是子卷的前提下才能做。** 如果视图内容清单所在的目录不是子卷,快照会直接失败;建树的时候就要把它建成子卷。
3. **页缓存那一项要在每个档位之间清空缓存,并复核清空是否生效。** 清空之后要读一次可用内存与页缓存统计确认数值确实回落,不能只发一条清空命令就当它成功了。
4. 软件与数据版本、随机种子、每档样本数一并写进原始结果的元数据文件,和既有实验计划的做法保持一致。


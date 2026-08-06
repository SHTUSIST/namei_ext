# 给合作者 · 技术担忧清单 v3:与 overlayfs 的定位与取舍(2026-08-05)

本文接着 `docs/review/技术担忧清单-2026-07-16.md` 往下写,是这个系列的第三版。它只聚焦一件事:**这个机制相对 overlayfs 应该被放在什么位置,以及由这个定位直接推出的、必须补做的那个实验。** 配套的实验计划在 `docs/tmp/2026-08-05-rq2-view-supply-cost-experiment-plan.md`。

## 先说这一轮做成的四件事(以下均经我们核实)

1. **检查点、恢复与迁移这个案例,从三次尝试失败变成通过,并已经过独立复核。** 这一组包含三次全新虚拟机启动、九个真实的 DMTCP 检查点与重启条件(DMTCP 是一个在用户态给运行中的进程拍快照、之后再恢复的工具),以及三组撤销映射的对照。九个检查点镜像、18 条应用观测、165 条控制器观测、108 行前后下层对象比对,全部通过。

2. **超算文件暂存这个案例同样从三次失败变成通过。** 这一组我们亲手读了原始结果文件核对,原文是:`{"boots": 3, "source_passes": 3, "namei_ext_passes": 3, "withdrawn_passes": 3, "mappings": 141, "selections": 141, "selection_hits": 204, "identities": 141, "preserved": 141, "permission_probes": 3, "withdrawal_controls": 3, "verdict": "supported"}`。其中 141 = 47 × 3,与三次启动、每次 47 个对象一致。

3. **选择边界之后的语义延续拿到了正式结果。** 三次启动、16 个案例 80 个操作的矩阵,48 个直接路径案例与 48 个选中路径案例全部通过;一个已经打开的选中目录,在策略被拆除之后仍然能完成基于目录描述符的创建、写、读、改名、删除,并且不再进入策略程序。

4. **论文故事收紧成了这一句**:`We argue that dynamic filesystem views are a pathname late-binding problem, not necessarily a new-filesystem problem.`

这四件事都是实打实的实验产出,尤其是前两件都是在三次失败之后重新做通的。下面的内容不改变对它们的评价,只是把我们这一轮审查里对论文最有用的技术发现交出来。

---

## 一、我们此前写错、现在更正的两处

这两处是我们自己在早先评审文档里写得不准确的地方,放在最前面。

**更正一:overlayfs 的复制上来这一步,不一定是整文件拷贝。**

我们此前写过"overlayfs 第一次写要把整个文件从下层复制到上层"。抓主线内核源码之后发现这个说法不准确。`fs/overlayfs/copy_up.c` 里的注释原句是 `Try to use clone_file_range to clone up within the same fs`,对应代码是 `cloned = vfs_clone_file_range(old_file, 0, new_file, 0, len, 0);`。克隆成功就直接结束,失败才走逐段拷贝。

所以,当上层目录和下层目录处在同一个支持块级克隆的文件系统上时(块级克隆指文件系统只复制一份指向同一批磁盘数据块的记录、不搬运数据本身,btrfs 和 XFS 支持),这一步本身就是一次块级克隆,不搬数据。整文件真拷只发生在克隆不可用的时候,例如底层是 ext4,或者上下层跨了不同的文件系统。

**更正二:overlayfs 的层深代价不在读数据上。**

我们此前写过"overlayfs 的深度代价出在读上"。准确的说法是:**它出在冷的路径解析上,也就是查找与打开这一段,不出在已经打开之后的逐字节读上。**

依据是主线内核 `fs/overlayfs/file.c`。`ovl_read_iter` 先取出真正的底层文件 `realfile = ovl_real_file(file);`,再把读整个交给它:`return backing_file_read_iter(realfile, iter, iocb, iocb->ki_flags, &ctx);`。`ovl_mmap` 也是同样的写法:`return backing_file_mmap(of->realfile, vma, &ctx);`。文件一旦打开,后续的读就不再经过层叠逻辑。

**这一条反而对本项目有利。** overlayfs 里剩下的那笔与层数有关的代价,恰好就是把名字翻译成对象这一步,正是本机制所在的那一层。

---

## 二、把"一份任务专属视图"拆成七件事

用一个具体场景说明。一个 AI 编码智能体在 `/workspace` 下干活,要给它一份属于它自己的 `/workspace`。任何一个机制想做成这件事,都必须回答七个问题:

1. **绑定**:这个名字对应哪个真实文件。
2. **可见性**:列目录的时候看到哪些名字。
3. **写落到哪里**:新写的数据存到什么地方。
4. **写的隔离**:这个任务的写会不会被别的任务看到。
5. **第一次写的代价**:第一次修改一个原有文件要付出多少。
6. **提交与丢弃**:改完之后怎么合并回去,或者怎么整份扔掉。
7. **每份视图的资源与生命周期**:一份视图在内核里占什么、由谁负责回收。

下表按这七件事对现有机制逐格填写具体做法(不是打勾,是写它实际怎么做的):

| | 1 绑定 | 2 可见性 | 3 写落到哪里 | 4 写的隔离 | 5 第一次写的代价 | 6 提交与丢弃 | 7 每份视图的资源 |
|---|---|---|---|---|---|---|---|
| overlayfs 一次挂载 | 做,按层叠顺序找 | 做,用白障条目盖住下层的名字 | 做,写进上层目录 | 做 | 先试块级克隆,不成才逐段真拷 | 做,上层目录就是这份差异 | 一次挂载 |
| 挂载命名空间加绑定挂载 | 做,挂什么看什么 | 做 | 不做 | 不做 | 不涉及 | 不做 | 一份完整挂载表的复制 |
| 块级克隆(btrfs 子卷快照、reflink) | 不做,克隆树在另一条路径上 | 不做 | 做 | 做 | 只复制被改动的区段 | 做,丢掉快照即可 | 一个子卷;逐文件克隆则每文件一个新记录 |
| composefs | 做,可表达任意逐文件子集 | 做 | 不做,只读 | 不做 | 不涉及 | 不做 | 一个镜像加一次挂载 |
| namei_ext | 做,每次查找按任务现算 | 做,可让一个名字返回不存在 | 不做 | 不做 | 不涉及 | 不做 | 几条规则(未测) |

(表中两个名词先解释一下。"白障条目"是 overlayfs 用来在上层目录里标记"下层的这个名字要当作不存在"的特殊条目,详见下面第五节。"挂载命名空间"是内核为一组进程各自保存一份挂载表的机制,不同命名空间里同一条路径可以挂着不同的东西;"绑定挂载"是把一个已有的目录再挂到另一条路径上,例如把 `/srv/repo-a` 绑定挂到 `/workspace`。)

**2026-08-05 补充:这张表每一格写具体做法而不是打勾,是有意为之。** BranchFS 用过一张逐项打勾的对照表,其中两格被它自己引的参考文献推翻——一格是"联合文件系统没有把改动合并回父层的原生能力",而 device-mapper 的 `snapshot-merge` 目标做的正是这件事;另一格是"挂载需要 root 权限",而 `userxattr` 挂载选项使得在用户命名空间内以非 root 身份挂 overlayfs 可行。(这两条转引自检索环节,我们本轮未重抓原文,写进论文前请再核。)**逐项打勾是高风险体裁:一格出错,整张表都会被怀疑。** 所以本表每一格都写清实际做法与适用条件。

从这张表能读出三条观察:

- **overlayfs 自己的接口本来就把这七件事切成了两组。** `lowerdir` 回答的是第 1、2 件,`upperdir` 回答的是第 3 到 6 件。它并没有把两组混在一起实现,只是把两组绑在了同一次挂载里。本机制做的事情,是把第 1、2 件从挂载里拿出来,改成每次查找按任务现算。

- **正因为两组绑在一起,想改"看什么"就必须重做整个挂载**,而重挂会把 `upperdir` 一并重置;何况正在跑的进程占着挂载点,卸不掉。内核官方文档从另一面把这一点写死了:`At mount time, the two directories given as mount options 'lowerdir' and 'upperdir' are combined into a merged directory.`,以及 `Changes to the underlying filesystems while part of a mounted overlay filesystem are not allowed. If the underlying filesystem is changed, the behavior of the overlay is undefined, though it will not result in a crash or deadlock.`

- **这七件事里,第 1、2 件与第 3 到 6 件是可以分开的两组,而今天所有现成机制都是两组打包提供。** 这正是本项目主张的落点。

---

## 三、由此得到的重新定位(这一节最重要)

**本机制真正应该拿来比较的对象不是 overlayfs,而是挂载命名空间加绑定挂载。**

理由直接来自上面那张表:overlayfs 占的是第 3 到 6 件,挂载命名空间加绑定挂载占的是第 1、2 件——和本机制填在同一格里的是后者。**所以 overlayfs 与本机制是可以配合使用的,不是互相替代的**:下面用 overlayfs 或块级克隆提供写隔离,上面用本机制决定哪个任务看到哪棵树。

工业界的切法印证了这一点。Docker 的 btrfs 存储驱动官方文档原句是 `The container's writable layer is a Btrfs snapshot of the final image layer, with the differences introduced by the running container.`(这一句我们亲手核实过);zfs 存储驱动文档写的是 `A container is a ZFS clone based on a ZFS Snapshot of the top layer of the image it's created from.`(**这一句由检索环节抓取,我们没有重抓原文,引用前请再核一次**)。而"同一条路径在不同容器里指向不同东西"这件事,靠的是挂载命名空间。**块级克隆负责内容隔离,挂载命名空间负责命名——正好是上表的那两组。**

这个重新定位有三个好处:

1. **论证需要回答的问题变窄了。** 不必再回答"overlayfs 连写隔离都给了,你凭什么",因为两者做的不是同一件事。要回答的问题变成一个更窄的:相对于给每个任务开一个挂载命名空间加绑定挂载,本机制省了什么。
2. **要测的东西变明确了。** 对照组从"每视图一次 overlayfs 挂载"换成"每视图一个挂载命名空间加绑定挂载"。
3. **但它没有解决任何数据缺口。** 按视图数量变化的成本曲线,一条都还没有测出来。

**2026-08-05 补充:一个必须正面回答的竞争者。**

DeltaBox 已经把我们想讲的这条缺口写进了它自己的论文当动机。原句我们亲手抓 arXiv 正文第 4.1 节核实过:`Standard Linux overlayfs fixes its layer stack at mount time; reconfiguring it requires an umount/mount cycle, impossible while the agent holds open files and untenable at the checkpoint rates MCTS demands.`(标准的 Linux overlayfs 在挂载时就把层栈固定了;要重新配置就得卸载再挂载一次,而在智能体还握着打开的文件时这做不到,在蒙特卡洛树搜索所要求的检查点频率下也撑不住。)

**但它解决这条缺口的办法不是另造一个机制,而是直接改 overlayfs**:用 XFS 加 reflink 作底,再配一个改过的 overlayfs 内核模块,通过一个自定义的 ioctl(ioctl 是让用户态程序向内核下达特定控制命令的接口)在不卸载的情况下重排层栈。

于是审稿人会问:既然给 overlayfs 加一个重排层栈的 ioctl 就能做到运行中换视图,为什么要在名字解析路径上另开一个扩展点?**我们唯一站得住的答复是粒度**——改层栈是按挂载生效的,同一个挂载点上的所有使用者一起换;我们的判定是按任务的,同一个挂载点上不同的任务可以同时看到不同的东西。**这个差别必须在论文里主动写出来并引用 DeltaBox,不能等审稿人提出来。** 完整调查见 `docs/review/近邻系统如何论证值得新加一个机制-2026-08-05.md`。

**一条必须守住的纪律:不许写"挂载方案做不到按任务给不同视图"。** 我们在 2026-07-10 实测过:在 root 权限下,或者无特权但提前建好用户命名空间的情况下,同一个进程的两个线程可以各自进入不同的挂载命名空间。**挂载路线做得到。** 真正的差别是:每个线程要付一份完整挂载表的复制,所有挂载操作要在一把全局锁后面排队,而且该线程会永久失去与兄弟线程共享当前工作目录的能力。**这是成本与粒度上的差别,不是能力上的差别。** 这句话写错,审稿人查一下文档就能推翻整段论证。

**2026-08-05 补充:再加两条同类禁令,都有近邻系统栽过的实例。**

**其一,不许写"挂载不能撤销已经可见的路径"。** YoloFS 论文写过这句话的英文版本(`it can expose additional paths, but it cannot revoke access to visible paths`),它不成立:`mount_namespaces(7)` 手册页自己就给了反例,我们亲手抓原文核实过——执行 `mount --bind /dev/null /etc/shadow` 之后 `cat /etc/shadow` 没有任何输出,手册的解释句是 `The above steps, performed in a more privileged mount namespace, have created a bind mount that obscures the contents of the shadow password file, /etc/shadow.` 撑得住的是它背后的意思,但必须换成准确表述:挂载是一次性布置好的静态安排,不是每次访问重新走一遍的判定;而且已经打开的文件描述符撤不掉。

**其二,不许写针对 overlayfs 的成本论证。** 两个挂载选项会当场推翻它,官方文档原句我们亲手核实过:`metacopy` 使得 `overlayfs will only copy up metadata (as opposed to whole file), when a metadata specific operation like chown/chmod is performed.`,而且 `The data will be copied up later when file is opened for WRITE operation.`;`redirect_dir` 使得改目录名时 `the directory will be copied up (but not the contents). Then the "trusted.overlay.redirect" extended attribute is set to the path of the original location from the root of the overlay.` YoloFS 那条"镜像基线树太贵"的论证正是因为没考虑这两个选项而不成立。

---

## 四、一个必须补进论文限制一节的句子

设计文档写了 `Data, writes, permissions, page cache, and persistence stay lower-filesystem owned.`,论文讨论一节也写了需要解决写冲突时应当用 FUSE 或自定义文件系统。**但仓库和论文里没有任何一句写出这句话的具体后果**——我们搜过设计文档、实现文档与论文全部章节:

> 两个任务如果选中同一个下层对象,其中一个写入之后,另一个会立刻看到;而且那个对象是原始文件本身,不是副本,所以基线被就地改掉,回滚做不到。

因此本机制只有两种正确用法:**被选中的目标本身是一棵事先隔离好的树**(用块级克隆,或者事先做好的 overlayfs),或者**只用在读为主、几乎不写的场景**(构建动作的声明输入、服务配置、工具链、授权给沙箱应用看的文档、超算的库暂存)。建议把上面那句话直接写进限制一节,不要等审稿人自己想到。

---

## 五、composefs 必须被正面回答

composefs 已经能表达任意的逐文件视图:用一个 EROFS 元数据镜像描述目录树,文件内容放在按内容寻址的共享存储里,靠 `trusted.overlay.redirect` 扩展属性让 overlayfs 找到真正的文件。官方仓库原句是:`the underlying non-empty data files can be shared in a distinct "backing store" directory. The EROFS filesystem includes trusted.overlay.redirect extended attributes which tell the overlayfs mount how to find the real underlying files.`;`shared files only need to be stored once, yet can appear in multiple mounts`;`data files are shared in the page cache`。

**所以"overlayfs 组合的单位是目录、表达不了任意逐文件子集"这条论证要收窄。** 未经加工的 overlayfs 确实要逐个建条目:隐藏一个文件要建一个白障条目,内核文档原句是它被创建成 `a character device with 0/0 device number or as a zero-size regular file with the xattr "trusted.overlay.whiteout"`。但 composefs 已经把这件事做成了产品。

本机制与 composefs 的差别只有四点:**不构建镜像、不挂载、可以在任务还跑着的时候改答案、被选中的对象可以是可写的。差别不在能不能表达上。** 建议主动把这一点写进相关工作,不要等审稿人把 composefs 摆上桌。

---

## 六、块级克隆这条路线的账(供设计一节参考)

有人会问:为什么不干脆给每个任务做一份块级克隆?回答分三条。

1. **这条路线本身成立,而且已经出货多年**(见第三节 Docker 的两条引文)。
2. **但它的代价是内存里每份克隆各缓存一份。** Linux 的页缓存(内核为文件内容维护的内存缓存)是按每个文件各自的地址空间组织的,两个通过块级克隆共享同一批数据块的文件,在内核看来是两个不同的文件记录。LWN 2022 年 5 月 24 日 Jake Edge 的文章原句(我们亲手核实过)是:`When two files share an extent, their inodes point at the same data blocks on the disk, though they seem to be completely independent files.`,以及 `When those files are read, each gets copied separately into the page cache. That wastes memory, but there are also other costs: reading from the disk, computing checksums, decompressing, and so on.` 二十个智能体各克隆一棵仓库树、再各跑一遍全文搜索,同样的内容会在内存里存二十份。
3. **它不解决命名问题。** 克隆出来的树在另一条路径上,要让它出现在固定路径上,仍然要用挂载命名空间。

---

## 七、另外两件仍然悬着的事

**第一件:路径身份实验,一天能做完,至今没做。**

问题是:一个进程通过 `SELECT_TARGET` 选中目标之后,去问"我这个文件的真实路径是什么",拿到的是它请求的逻辑路径,还是后端的真实路径?我们搜过设计文档、实现文档、论文全部章节,以及 `tests/`、`experiments/`、`bpf/`,`realpath`、`getcwd`、`/proc/self/fd` 这三个关键词一处相关的都没有。

**为什么它决定成败**:Bazel 官方博客承认符号链接森林有一个正确性缺陷,原句是 `Some tools (e.g. some compilers or linkers) will decide to extract the real path of such symlinks and work off that path. These tools may end up "discovering" and consuming undeclared files that are siblings of the symlink's target.` 构建动作沙箱这个案例唯一能站住的论证就是"我们没有这个缺陷";如果本机制返回的是后端真实路径,这条论证就不存在。**(这是我们从设计文档推出的判断,不是实测结果。)**

建议的最小实验:在 `SELECT_TARGET` 之后记录 `getcwd()` 的返回值、`/proc/self/fd/N` 指向哪里、`realpath()` 返回什么、`fstat` 拿到的设备号与 inode 号;再用一个真的会做路径规范化的工具(编译器生成依赖文件、链接器处理 rpath)验证它会不会消费到未声明的兄弟文件。

**第二件:冷缓存的元数据开销仍然是零数据。**

这条线两次协议都用满三次尝试之后关闭,最近一次的原因是客户机的打开文件数硬上限停在内核初始的 4,096,控制器在挂载前拒了 FUSE 那一支。项目文档自己的结论是 `Cache-cold and broader mutating-metadata cost therefore remain unresolved`。文件系统方向的审稿人第一个问题通常就是冷缓存,建议不要让它一直空着。

---

## 结尾

一句话:这一轮最有价值的结论不是又找到一个缺口,而是**把要比较的对象认对了**——不是 overlayfs,是挂载命名空间加绑定挂载。认对之后,要证明的东西就落到了一个能证明的范围里。具体怎么证明,见配套的实验计划 `docs/tmp/2026-08-05-rq2-view-supply-cost-experiment-plan.md`。


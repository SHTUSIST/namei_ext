# 文档二:namei_ext 深度研究 —— 撞车检索 · 对抗性审查(经核验)· 研究级设计方向

> 本文档由一个 42-agent / 6-阶段的 deep-research 工作流产出后,经我人工去重、核对引用、剔除无意义质疑后综合而成。
> 三大部分对应你的三项要求:**(A) 文献撞车检索**(发表 + arXiv + 内核社区/推特/论坛)、**(B) 对抗性审查**(质疑本身也经过"对质疑的核验",剔除站不住的)、**(C) 研究级设计方向**(非增量/工程,目标 FAST-2026 / OSDI)。
> 目标会场:FAST-2026 fall / OSDI。撰写日期:2026-06-26。

---

## Part A. 撞车检索与先验工作图谱

### A.0 总结论:**partial-collision(部分撞车)**

四条检索 lane 的结论需要被"诚实合成",而不是简单平均:

- **顶会(OSDI/SOSP/FAST/ATC/EuroSys/NSDI)与 arXiv 中,没有任何论文把 BPF 放进 VFS name resolution 去 redirect 同父组件。** 核心机制在**学术语料里无直接撞车**,设计空间格子是真实的。
- **但最强的先验工作活在学术语料之外**(内核补丁、邮件列表、产品)。对系统论文,这些**非归档的内核工件同样算 prior art,审稿人一定会提**。
- 因此:**"BPF 能在内核里 redirect lookup/readdir" 这个能力本身已被 FUSE-BPF 确立,不能当作贡献;真正未被占据的是"打包方式"**(generic、无 FUSE、cgroup 作用域、最小动作集、内核保留对象所有权)。

### A.1 最危险的三个撞车(论文必须正面交锋,否则被当增量)

| 先验工作 | 撞车级别 | 为什么危险 | namei_ext 必须论证的差异 |
|---|---|---|---|
| **FUSE-BPF**(Daniel Rosenberg / Google-Android,LWN 2022–2024,[lwn.net/Articles/937433](https://lwn.net/Articles/937433/)) | **direct(机制)** | **它的官方示例就是 namei_ext**:"一个给文件名加字符的 stacked fs 只需为 lookup 和 dir-read 实现 filter"——行为上就是 `tool→tool.real` 的 lookup+readdir 改名,由内核 BPF 完成,**已在 Android 出货**。 | (1) namei_ext 钩的是**通用 VFS namei 路径**(walk_component/open_last_lookups/iterate_dir),作用于**任何已挂载的下层 fs**,**无 FUSE 挂载、无 upper/lower backing-file 模型、无 per-inode struct_ops**;(2) **一个 cgroup 作用域程序** vs FUSE-BPF 的 per-FUSE-inode 全 op 面;(3) 动作集是内核校验的极小 PASS/REDIRECT-同父。**关键有利点:FUSE-BPF 的 BPF 部分至今未合入上游(6.9 只合了 plain passthrough),它被卡的原因是 scope/重复 VFS 逻辑/维护负担,而非安全**——正是 namei_ext 极小动作集设计想回答的。 |
| **OverlayFS `redirect_dir` + `metacopy`**(内核文档) | **strong(机制)** | **内核里已经存在"把名字重定向到另一后端"**:`redirect_dir` 让 lookup 解析到异名 lower 目录(`overlay.redirect` xattr);`metacopy` 只 copy-up 元数据、**数据仍在 lower 层/page cache**——这正是 namei_ext"元数据受策略控制、读路径原生"的卖点。是每个 fs 审稿人开口第一问"为什么不用 overlayfs"。 | overlay 的重定向是**静态、目录粒度、随 copy-up/rename 落到磁盘 xattr、固定 mount option**;namei_ext 是**运行时、最终组件粒度、按 op-context/cgroup 编程、无 union、无 copy-up**。差异真实但**窄**,必须被论证+测量,不能只在表格里写"固定 vs 可编程"。 |
| **DeltaBox / DeltaFS / DeltaCR**(IPADS/SJTU,arXiv [2605.22781](https://arxiv.org/abs/2605.22781),2026) | **direct(use-case)** | 团队**不知道的最锋利 use-case 撞车**:同样主打 agent sandbox lifecycle(fork/fanout from pinned state + 毫秒级 checkpoint rollback/restore)、同样 SWE-bench framing,2026 顶级组,给出 ~1.12ms checkpoint、~5ms restore、1419 forks/s、~11MB/child,**且支持写与内存回滚**。FAST/OSDI 审稿人会知道它。 | DeltaBox 做的是 overlay-CoW + 进程 C/R 的 snapshot/fork 引擎,**严格比 namei_ext Phase-1 做得多**。**namei_ext 绝不能争 fork/rollback 的冠**,必须重新定位为"可被这类系统复用的、可编程、零挂载、每组件的路径-视图原语",并明确让出 fork/checkpoint 轴。 |

### A.2 强近邻:framing twins / mechanism twins(必引 + 必区分)

- **cache_ext(SOSP'25,[dl.acm.org/10.1145/3731569.3764820](https://dl.acm.org/doi/10.1145/3731569.3764820))** — **最强 framing 双胞胎**:同样 sched_ext 式 BPF 定制点挂在核心内核子系统、`_ext` 命名、static-branch 快路径、per-app 隔离,同年。审稿人会条件反射读成"cache_ext 的 namei 版"。差异:它选"evict 哪个页"(资源管理),从不碰 dcache/namei/目录枚举,无 redirect/alias 概念。namei_ext 必须论证 **hot-path lookup/readdir 策略比 eviction 难在哪**(跨 syscall 一致性、RCU-walk、per-dirent dispatch)。
- **sched_ext(内核 6.12)** — 显式类比与命名来源。**警告:`sched_ext-for-X` 已是公认 meme(sched_ext→cache_ext→gpu_ext),类比本身不能算贡献**,无归档论文,引内核文档/LWN。
- **gpu_ext(arXiv [2512.12615](https://arxiv.org/abs/2512.12615))** — **同一个 eunomia-bpf 实验室**的另一个 `_ext`。必须做"同实验室又一个 _ext"的自我定位,否则审稿人会质疑是流水线刷点。
- **ExtFUSE(ATC'19,[usenix.org/.../atc19-bijlani.pdf](https://www.usenix.org/system/files/atc19-bijlani.pdf))** — 最接近的**已发表** eBPF-at-FS-boundary;FUSE-BPF 的概念祖先。差异:绑 FUSE 模型(必须有 FUSE fs+daemon,BPF 只缓存/短路),目标是加速而非把 redirect 当一等动作。
- **FiST / Wrapfs / Unionfs([filesystems.org](https://www.filesystems.org/))** — "在 lookup 里改写/合并名字"的历史正源(stackable fs 自定义 lookup)。差异:它们插入一整层**拥有自己 dentry/inode/file_operations** 的 fs,翻倍 VFS 对象、编译期固定;namei_ext 插一个 BPF 决策、无新 fs 层、无对象翻倍。
- **SandFS(LWN 2019,[lwn.net/Articles/803890](https://lwn.net/Articles/803890/))** — 最接近的**非正式**"BPF 改写 fs-op 参数"陈述(改 Wrapfs,按 BPF 改 op 参数如强制只读)。差异:它改的是 mode/flag(沙箱/访问控制),**不 redirect 名字解析到哪个 inode**,且需挂 stacked wrapper。
- **BPF `sk_lookup`(内核文档)** — **最接近的"让 BPF 决定一次 lookup 解析到什么"的先例**(socket 选择)。审稿人会问"这不就是文件版 sk_lookup 吗"。差异:网络 socket 选择对象模型远简单——无 dentry/inode/mount/page-cache、无 RCU-walk、无 dcache 一致性、无 per-dirent 枚举。namei_ext 必须正面回答"VFS 路径解析为何是更难、更不同的问题"。
- **openat2 `RESOLVE_*`(LWN [779649](https://lwn.net/Articles/779649/))** — 证明**内核已接受调用方提供的 in-namei 策略**(NO_SYMLINKS/BENEATH/IN_ROOT)。差异:它是 **restrict-only + 固定 flag 集**,从不 redirect,也不可编程。是 namei_ext"redirect+可编程"novelty 的天然磨刀石。
- **composefs / EROFS imagefs(LWN [917324](https://lwn.net/Articles/917324/))** — **团队材料中零提及的盲点**。它做**内容寻址、fs-verity 校验、page-cache 共享**的内核态解析,与 namei_ext "元数据指向别处、数据原生"直觉相同,且是 content-verified-cache use case 的最强竞争者。差异:name→backing 映射在**镜像构建期烘焙、只读、内容寻址**,非运行时可编程。**结论:content-verified-cache use case 应放弃,RIT 不要走 content-addressed 框架**(否则正撞 composefs),改走 per-task **versioned** 间接。
- **FUSE passthrough(内核 ≥6.9)** — **对"design-position"论证的威胁**:它让 FUSE 把数据 I/O 直通下层/原生 page cache,削弱了"FUSE 增加穿越/重复实现下层"的框架。namei_ext 的差异必须**收窄到 metadata/lookup 策略,而非数据路径**。
- **Bento(FAST'21 best paper)** / **XRP(OSDI'22 best paper)** / **λ-IO(FAST'23)** — 安全内核 fs 扩展谱系 / BPF 在内核 I/O 数据面。都不碰 name resolution;引用以划清"控制面策略 vs 数据面卸载"的界。

### A.3 use-case 近邻:agent-sandbox 赛道(判断转向 framing 是否拥挤)

**裁决:agent-sandbox-FS 赛道在 2026 已相当拥挤,且竞品大多能力更强(支持写/隔离/快照)。** 主要竞品:

- **YoloFS / AgentFS(Microsoft,SOSP 2026,arXiv [2604.13536](https://arxiv.org/abs/2604.13536))** — coding-agent fs,目标 safety/undo + 权限门控(staging/overlay),控制写是否 commit;非 in-VFS redirect。
- **Sandlock(arXiv [2605.26298](https://arxiv.org/abs/2605.26298))** — 用 Landlock+seccomp 约束 agent;占的是 namei_ext **明确免责**的 allow/deny/confine 轴。
- **AgentFS(Turso,FUSE+SQLite CoW)** / **Fault-Tolerant Transactional Sandboxing(arXiv [2512.12806](https://arxiv.org/abs/2512.12806))** — 仍付 FUSE 税、做隔离/事务回滚而非 redirect。
- **既有"廉价可写 fanout"基线(对主打动机的釜底抽薪)**:overlay snapshots + btrfs/XFS **reflink CoW**、**git worktree**(Cursor 2.0 Agents Window 已用)、OSGym(reflink CoW replicas,arXiv 2511.11672)、E2B/Modal directory-snapshots/Daytona/OpenAI Codex(clone@SHA+container cache)/microsandbox。**它们都提供完整可写分支 + 近原生读 + 毫秒级 setup——正是 agent workspace fork 要的,而 namei_ext 明确不提供写。**
- **SWE-Hub / SWE-smith / SWE-Mirror(arXiv 2603.00575 等)** — "降低 workspace 物化成本"的最强量化动机,**但社区采用的解法是镜像共享/dedup/container-free,已经在不用路径 hook 的情况下攻击该成本**——既支撑问题陈述,又说明这个 niche 正被别人填。

### A.4 这个 idea 在社区被公开提过吗?

**答:部分被提过,且这点很关键。**

- **FUSE-BPF(Google,LWN 2022–2024)公开提出并部分实现了"内核 BPF 在 lookup/readdir 期间改写名字",其官方示例行为上就是 namei_ext 的 redirect,且在 Android 出货。** BPF 部分**未合入上游**(6.9 只合 plain passthrough)。
- **SandFS(2019)** 提过"BPF 在每个 fs op 上咨询策略并可改 op 参数"(改的是 mode/flag,非 resolved name)。
- **"generic BPF filesystem policy 该不该做" 正被相关维护者实时公开辩论**:Corbet 的 *"Famfs, FUSE, and BPF"*(LWN,2026-04,[1068686](https://lwn.net/Articles/1068686/))——Miklos Szeredi 倾向探索通用 BPF 路径,Christoph Hellwig 想要一个收敛接口,Darrick Wong 对"BPF 当 maintainer-bypass"存疑。
- **没找到的**:没有任何公开帖/talk/邮件/preprint 提出**这个确切打包**——generic、sched_ext 式、cgroup 作用域、钩 walk_component/open_last_lookups/iterate_dir、把组件 redirect 到同父 sibling、独立于任何 fs、无 FUSE。
- **接收信号(有利)**:维护者**没有**在安全原则上否决"BPF 进 lookup 路径"(LSFMM 2023 的担忧被当作"不比其它 BPF 更危险"而搁置);卡住 FUSE-BPF 的是 **scope/VFS 逻辑重复/维护**——正是 namei_ext"最小动作集、内核保留机制"设计想回答的。

### A.5 必引清单 + 必须正面回答的"为什么不是 X"

**必引(≥ 应单列对比段或表格行)**:FUSE-BPF、OverlayFS redirect_dir+metacopy、ExtFUSE、sched_ext、cache_ext、gpu_ext(同实验室)、FiST/Wrapfs/Unionfs、SandFS、sk_lookup、openat2 RESOLVE_*、Landlock/BPF-LSM/fanotify、composefs/EROFS、FUSE passthrough、Bento、XRP、Famfs-FUSE-BPF 维护者辩论(LWN 2026)、DeltaBox、YoloFS/AgentFS、Sandlock、overlay-reflink/git-worktree、E2B/Modal/Daytona/Codex、SWE-Hub 物化成本。

**审稿人开口必问的"为什么不是 X"(论文必须有专段回答)**:① 为什么不是 FUSE-BPF? ② 为什么不是 overlayfs redirect_dir? ③ 为什么不是 sk_lookup-for-files? ④ 为什么不是 composefs? ⑤ 既然 DeltaBox/overlay-reflink/git-worktree 已廉价做 fork/rollback,namei_ext 在 agent 场景还剩什么? ⑥ 既然 `sched_ext-for-X` 是 meme,你的 VFS-specific 贡献到底是什么?

---

## Part B. 对抗性审查(经核验)

### B.0 方法与统计

- 4 个独立审查视角(内核/VFS 正确性、新颖性/抽象、评估严谨性、动机/转向)共产出 **28 条质疑**。
- 每条再经一个**对抗性核验 agent**裁决三问:**是否 grounded 在真实系统/代码/结果?是否 material 到 FAST/OSDI 可发表性?是否经得起作者最强反驳?** → **27 条通过、1 条被剔除**(下文 B.4)。
- 我再按结构**去重**(多个视角命中同一根因),得到 **20 条 distinct 质疑**:**3 条 fatal、13 条 major、4 条 moderate**。每条均带**代码/文档出处**与**最强作者反驳**,以便经得起再质疑。

> 说明:质疑保留率高(27/28)不是核验不严,而是这 4 个审查视角本身被强约束在"必须引代码行、不许 strawman"。被剔除的那条恰恰是唯一一条事实性站不住的(B.4)。

### B.1 抽象与新颖性(最致命的一组)

| # | 严重度 | 质疑(去重后) | 代码/文档出处 | 最强作者反驳 |
|---|---|---|---|---|
| **C1** | **FATAL** | **新颖抽象全在 future work,已实现的被既有机制 subsume**:论文卖的"per-workload、versioned、可编程路径解析(fork/fanout、rollback、versioned dep/cache view、store→project 映射)"全部未实现——每个都需要 cross-directory backing 或 view_id/epoch,而团队自己的 eval plan 把 `view_redirect.bpf.c` 列为 future work、把 real store mapping 标 "blocked"、把 C1-C3 阻塞在缺失能力上。已实现的"同父单组件改名"被 symlink/bind/overlay 覆盖。 | `osdi-evaluation.md`(view_redirect 为"OSDI 主力 policy"但未实现);`research_plan.md` milestone 11 | 这是 PoC 阶段;论文投稿前会实现 view_redirect。**但:在它落地并测出正向前,这是 fatal。** |
| **C2** | major | **宣传的 `(parent path + component + context)` 三元组实际只有 `component` 一个字段**:`namei_ext_init_ctx` 只填 event/flags/name_len/name_hash/name,**从不填 `cgroup_id`(恒为 0)**,且**无任何父目录身份字段**。`name_hash` 不是逃生舱(它被 parent-dentry 指针 salt,verifier-bound 程序无法先验预测)。决策域 = 组件名。 | `fs/namei_ext.c: namei_ext_init_ctx`;`bpf/include/namei_ext.h:23`(cgroup_id) | 未来会填 cgroup_id、加 parent key。但当前 ABI 与"可编程路径解析"叙事有实质落差。 |
| **C3** | major | **sched_ext 类比夸大 policy surface;唯一策略是常量函数**:`redirect_alias.bpf.c` 是硬编码字节比较 `tool↔tool.real`、2 动作集、无 map、无状态、忽略 cgroup_id,**未行使任何可编程性**。且 `sched_ext-for-X` 已是 meme(cache_ext/gpu_ext),类比本身非贡献。 | `bpf/policies/redirect_alias.bpf.c` | 动作集小是刻意的安全设计;可编程性体现在未来 map-driven view_redirect。**但当前无证据。** |
| **C4** | major | **没有 mount-ns + overlay/bind/symlink 做不到的能力,且唯一独占格无 motivating customer**:per-workload 分叉视图正是 mount ns + overlay 的本职。namei_ext 唯一独占的格是"**同 mount-namespace、不同 cgroup、不物化**的分叉解析"——这真实,但论文**没命名任何需要 cgroup-而非-namespace 作用域的 workload**(agent/build/serverless 各自都有自己的 mount ns)。 | `research_plan.md` design-space;`§1.3` 表 | 见 Part C 的 D2-1:per-task/per-thread(同地址空间)才是真正独占格——但**团队当前没论证它**。 |

### B.2 内核正确性与安全(VFS maintainer 视角)

| # | 严重度 | 质疑 | 出处 | 最强反驳 |
|---|---|---|---|---|
| **C5** | major | **重名→可证不连贯目录,且 coherence 不被内核强制**:当目录同时含别名与后端名(真 `tool` 与 `tool.real`),`namei_ext_filldir` 只改 name/namlen、保留原 d_ino/cookie,于是 readdir 经 PASS 发射真 `tool`、又把 `tool.real` 改名成 `tool` 发射 → **用户拿到两个 `tool`(不同 d_ino),且真 tool 的 lookup 被 redirect 抢走 → 后端名 `tool.real` 在枚举里消失**。C4 头条"lookup-set == readdir-set"**是策略作者义务,不是内核不变式**(survey 计划的"append synthetic alias"未实现)。 | `fs/namei_ext.c: namei_ext_filldir`(只改 name/namlen);`§4.3` 实现事实 2 | 策略作者保证别名与后端名不相交即可。**但"正确性靠策略作者"对系统论文是结构缺陷。** |
| **C6** | major | **`same-parent, single-component` 安全不变式是假象**:`namei_ext_redirect_valid` 只校验 redirect **KEY**(长度、拒 `/`/NUL/`.`/`..`),**从不校验 resolved OBJECT 的 d_type/mount 状态**。校验过的 sibling 名一旦在 parent 里查出,正常 VFS `step_into` 会跟随它——若它是 **symlink/mountpoint/automount**,解析就**重新跨目录/跨文件系统**;中间组件 redirect 进一步复合。 | `fs/namei_ext.c: namei_ext_redirect_valid`;hook 在 `walk_component`/`open_last_lookups` | 跨目录正是想要的(future)。**但当前把它当"安全收窄"宣传是错的——收窄只在 key 上,不在 landing object 上。** |
| **C7** | major | **redirect 把用户路径与 audit / path-MAC / execve / /proc 看到的 inode 解耦**:调用点在最终组件查找+安全检查前把 `nd->last` 换成 backing 组件,于是"内核保留全部 permission 检查"**只对 inode DAC 成立**:AUDIT 的 PATH 记录把用户名字 `tool` 绑到 `tool.real` 的 inode;**AppArmor 基于路径名的规则**、`/proc/self/exe`、`execve` 名字全部与进程请求的名字脱节。 | `phase1_design.md:99-101`(swap nd->last 在安全检查前) | inode DAC 未被绕过(真)。**但"kernel keeps all VFS safety"是 over-claim:审计准确性与 path-based MAC 被破坏,需降级为精确定理(见 D3-9)。** |
| **C8** | moderate | **`LOOKUP_CREATE + REDIRECT → -EOPNOTSUPP` 破坏常见 O_CREAT-on-existing 幂等写**:对带 O_CREAT 的叶组件 redirect 在校验前就返回 -EOPNOTSUPP,于是不仅挡了 create-through-alias,还挡了 `gcc -o tool`、`> tool`、`tee`、编辑器、构建产物等"目标已存在的幂等写"。 | `fs/namei_ext.c:74-76`;demo 别名恰是可执行 `tool` | Phase-1 刻意只读。**但若 agent/build 工作流写别名路径,这是真实功能缺口。** |

### B.3 性能(VFS maintainer + 评估视角)

| # | 严重度 | 质疑 | 出处 | 最强反驳 |
|---|---|---|---|---|
| **C9** | major | **RCU 整体拒绝对 cgroup 内所有 lookup 加税,上游近乎 non-starter**:策略生效时 hook 在 BPF 运行前对 `LOOKUP_RCU` 返回 `-ECHILD`,因 walk_component/open_last_lookups 每组件触发,**第一个 RCU-mode lookup 就把整条路径重启为 ref-walk**——于是 cgroup 内每个 lookup(含纯 PASS、>99% 不 redirect 的名字)都丢掉内核最热的 walk、付 dentry refcount/d_lockref 流量。这是 `lookup_native_hot` 1.526x 与 `pass_only` 慢于 native 的根因;REDIRECT 动作在不丢 RCU 时**结构上不可能 near-native**。 | `kernel-code-survey.md`(RCU 伪码);`fuse-baseline` 解读 | 首个 PoC 刻意简化生命周期。**但"cgroup 策略悄悄关掉所有进程的 RCU walk"VFS maintainer 会当 non-starter。需 D1-4 RCU-safe PASS。** |
| **C10** | major | **每-dirent BPF dispatch 为 O(entries) 且不可缓存**:`namei_ext_filldir` 对每个 dirent、每个 getdents64 chunk、每次重读都跑 cgroup BPF,内核无法先验知道哪些条目会被改写故不可缓存 → `readdir_alias_view` 4.374x native(~915ns vs ~209ns);落在主打 workload 热路径(git status / find / 扫 node_modules/.git)。 | `fs/namei_ext.c: namei_ext_filldir`;`§4.3` 实现事实 1 | 可加 readdir 快路径过滤。**但当前是结构性 O(entries),且与 C5 一同指向"readdir 设计需重做",见 D3-1。** |

### B.4 评估有效性

| # | 严重度 | 质疑 | 出处 | 最强反驳 |
|---|---|---|---|---|
| **C11** | **FATAL** | **主打 use case(agent sandbox lifecycle)零宏观证据**:无 fork/rollback/trace-replay wall-time、无 materialization-under-edit 数字、无 `workload/` 目录、无 SWE-bench/agent harness。命令-trace 管线只停在"下一步:建 collector、跑 3-task smoke"。唯一干净正向(W2 nginx)属于**另一个**(service-fixture)thesis。 | `2026-06-19 survey` "Immediate next steps";无 workload/ 目录 | 工作正在进行。**但投稿时若主 use case 仍零宏观证据,fatal。** |
| **C12** | major | **FUSE 基线是朴素 libfuse low-level strawman,且不可复现**:repo 唯一基准二进制 `bench/workloads/namei_ext_bench.c` 只有 native-vs-policy,**无 FUSE 源、无 FUSE 构建目标**;FUSE 数字只活在 docs 散文里、指向被 `.gitignore` 排除且磁盘不存在的 JSONL。且排除了直接可比的 **ExtFUSE / FUSE-passthrough / overlay redirect_dir**。 | `Makefile`/`mk`/`configs` grep 无 FUSE;`.gitignore` | 这是 OSDI eval 计划里的 future 强基线。**但当前"2-99x faster than FUSE"既不公平也不可复现。** |
| **C13** | major | **headline FUSE 倍率由 tmpfs 上最 trivial 纯元数据 op 撑起,一遇真实 per-op 工作就坍到 2.26x**:高端倍率(98.97x)全来自 hot-cache、in-memory fs(`kvm.mk` 用 `--overlay-rwdir /tmp`,bench root `mkdtemp("/tmp/...")`)上的纯元数据 op——路径解析≈100% 单 op 成本的 regime;唯一含真实 per-op 工作的 `exec` 只 2.26x。且与 eval 计划假设的 ext4 lower 不一致。 | `kvm.mk`;`namei_ext_bench.c:105`;`osdi-evaluation.md:273` | 微基准本就该隔离机制成本。**但 headline 倍率会被审稿人当 regime artifact。** |
| **C14** | moderate | **唯一延迟证据是 SAMPLES=1 smoke 微基准**:~4 条目 readdir、64 文件 walk,单点值,无 median/p95/p99 分布、无 bootstrap CI、无宏观 wall-time——**违反团队自己 ≥30 micro reps + 95% CI 的发布门禁**。 | `configs/benchmarks/phase1.mk`(SAMPLES=1) | smoke 本就不支撑论文主张(团队明说)。属"尚未做",非"做错"。 |
| **C15** | major | **无端到端胜绩;退到"降低物化成本"也只 W2 干净成立**:W3 输给物化 checkpoint-view 基线 setup;W1/W4 update 输给最优非-FUSE 基线;团队自己承认不能声称端到端加速。 | `fuse-baseline-and-native-overhead-interpretation.md` | 诚实记录是优点。**但意味着当前没有可支撑顶会的正向性能故事(除 W2 窄片)。** |

### B.5 动机 / agent 转向

| # | 严重度 | 质疑 | 出处 | 最强反驳 |
|---|---|---|---|---|
| **C16** | major | **agent 沙箱真正瓶颈 namei_ext 都不碰(Amdahl 上限)**:按作者自己的 SWE-bench harness 分析,生命周期主成本是镜像 build/pull、依赖安装(`before_repo_set_cmd`、`pip install -e .[dev]`)、测试执行(`pytest`,timeout 1800s/task vs reset/apply 的 60s)。namei_ext 只改最终组件解析,它能碰的操作(git reset/apply、fork/checkpoint)在已检出的 repo 上都是亚秒级。 | `agent-command-trace-workload-survey.md` 命令相位 | 可省 setup/物化。**但占端到端比例小,需证明 setup 在超大 fanout 下确实主导。** |
| **C17** | **FATAL** | **主打操作 fork/rollback 恰是作者自己数据显示既有廉价机制胜出之处**:W3 checkpoint 全 flag 负、自承"非真实 restore",物化 checkpoint-view 基线 setup 更低;W1(折进 trace replay)报告字符串…；而 **DeltaBox/overlay-reflink/git-worktree/CRIU 严格做得更多(写+内存回滚)**。 | `agent-sandbox-usecase-scope-adjustment.md`;`fuse-baseline` 解读;lit DeltaBox | 必须重定位为互补原语、让出 fork 轴(见 Part C)。当前定位下,fatal。 |
| **C18** | major | **免责"无安全/eval 隔离"掏空了 "sandbox" 一词**:主 use case 标题"agent **sandbox** lifecycle"、动机引 E2B/Modal/gVisor/Firecracker(都卖隔离),却同时免责安全隔离与 eval/secret 隔离。namei_ext 是 cgroup 路径 redirect、明确保留下层权限、不加 containment——它借了那些产品卖的紧迫感(no host escape / no secret exfil),却恰好免责了那个属性。 | `osdi-evaluation.md:475`;`§E` 动机;scope-doc | 我们只主张视图机制不主张隔离。**但用 "sandbox" 框架会引来 isolation 审稿期望,需改词或改框架。** |
| **C19** | major | **确定性 SWE-bench offline trace 移除了 in-kernel 可编程性唯一的理由**:用 eBPF 而非静态 redirect 表的全部理由是 **online 动态性**(view_id/epoch/canary/rollback/map-driven)。但评估**刻意**把 workload 固定成确定性命令 trace 并 offline 重放以去 LLM 方差——而预知的确定性 trace 恰是**预计算静态映射**最擅长的情形:N 个确定任务可预计算 N 套映射。 | `osdi-evaluation.md:124-168`;`survey` 5/16/499 | 见 D2-2/D1-4 View-epoch:加"运行中原子切换/rollback + fleet churn"这种静态表做不了的 live 轴来恢复理由。 |
| **C20** | moderate | **更强且与证据对齐的动机已在作者数据里:per-workload config/fixture/canary 替换,而非 agent fork/rollback**:机制真实约束(同父单组件、cgroup-global、读多写少、胜 FUSE 不胜 native)恰好契合"为运行中进程树透明替换一小组 config/cert/secret-decoy 文件",其现实替代是 FUSE overlay 或镜像重建、且隔离已被免责。 | `§4.4`;`fuse-baseline` W2 | 见 Part C:可作为更窄但更稳的 thesis 备选。 |

### B.6 被剔除的质疑(1 条)及"对质疑的质疑"

- **被剔除**:"固定 64 字节 name 窗口 + 全长 hash 让部分命名空间不可治理,且 content-cache(sha256 命名)正好落在边界"。
  - **剔除理由(核验裁决:nitpick/factually-off)**:该质疑断言截断是"静默"、且 parent-salted `name_hash` 是"唯一区分器"——**事实错误**:ABI 同时暴露 `ctx->name_len = name->len`(**未截断的全长**),且示例策略已对每次匹配做长度守卫;策略可用 `name_len` 与常量 64 比较来**检测**截断。前提既已站不住,质疑作废。
- 这一条恰好演示了核验机制如何剔除"读错系统"的质疑。**其余 27 条均经住"grounded? material? survives rebuttal?"三问。** 我额外做的去重确保最终 20 条不重复计分。

---

## Part C. 研究级设计方向(排序;已剔除纯增量/工程)

> 评审标准:novelty × 一周期可行性 × 会场契合 × 能否消解上面 fatal/major 质疑。**剔除一切"只是更快""只加基线""调 readdir"的工程改进。**

### C.0 单一最佳论文形态(核心结论)

> **不要 lead "比 FUSE 快",要 lead 一个"竞品结构上做不到"的能力。** 两条存亡级质疑是:(C1/C4)已实现抽象被 symlink/bind/overlay subsume;(C11/C15/C17)无正向性能结果。**唯一同时消解两者的,是 per-task、O(1)、无锁的 ephemeral 视图(下方 D1)。**
>
> 结构性论据(团队自己没用过、且 FRESH):`task->nsproxy->mnt_ns` 被一个进程的**所有线程共享**,所以同一 mount namespace 里的两个 task(更别说同一地址空间的两个线程)**在任何速度下都无法持有不同视图**;而 mount/umount 供给**串行化在全局 `namespace_sem`** 上,大规模 churn 产生**悬崖式退化拖垮整机**。FUSE 能 per-caller redirect 但每个元数据 op 付一次用户/内核穿越。**namei_ext 是唯一格:FUSE 级 per-task 灵活 + overlay 级原生元数据速度 + O(1) 无锁供给——恰好是 agent fanout regime。**

### C.1 推荐方向(rank 1–9,均 recommended)

| rank | 方向(kind) | 一句话 | 研究问题 / 为何非增量 | 消解哪些质疑 | 会场 |
|---|---|---|---|---|---|
| **1** | **One process, 10k workspaces**(capability)—**首选锚** | per-task/per-thread、地址空间内共享的 ephemeral 视图,作为 agent fanout 杀手应用 | mount 机制结构上做不到 per-task 视图、FUSE 做不到原生元数据速度——能否给出一个 **mount/overlay/bind 在任何速度下都无法提供** 的能力,并用 `namespace_sem` churn 悬崖做可测基线? | **C1/C4(subsume)+ C11/C15/C17(无正向)同时消解**;给出竞品-impossible 的独占格 | both |
| **2** | **Coherent-by-construction views**(abstraction)—**强制骨干** | 策略声明一个 renaming,内核**导出并强制其逆**,使 lookup==readdir 成为定理 | 把论文中心的**假命题**(lookup==readdir 靠策略作者)变成内核构造性不变式;并把 per-dirent readdir 成本折叠掉 | **C5(双 tool 不连贯)、C10(readdir O(entries))、C11/C-coherence** | both |
| **3** | **RCU-safe BPF**(mechanism)—**近乎必需** | 一套 verifier 纪律,让非 redirect lookup 留在 RCU-walk,仅在 REDIRECT 时 unlazy | 把失败 near-native 门禁的**最大根因**(整体 -ECHILD)变成可复用的 OSDI 级 **verifier 安全契约**;"如何在 RCU-walk 里安全跑策略"本身是研究问题 | **C9(RCU 税)**;恢复任何 near-native 正向故事 | both |
| **4** | **RIT:目录命名空间作为可编程地址空间**(mechanism)—最高 novelty 天花板 | 内核拥有的、per-cgroup、**return-by-id** 间接表("命名空间的页表") | 把 REDIRECT 从"改一个 sibling 字符串"升级为"按整数 id 索引内核拥有的、versioned 间接表(永不按指针)",在保持"内核拥有 dentry/inode/path 生命周期"的前提下实现 **cross-directory** backing | **C1/C2(抽象空洞)、C6(跨目录靠 sibling 副作用)** | OSDI |
| **5** | **Safe cross-directory views**(capability) | 内核校验的 target registry + **acyclicity/termination 证书** + epoch 一致枚举 | 给被推迟的"target registry + view_id/epoch"一个**研究骨架**(载入期非环/终止证明、枚举原子)而非 bolt-on 工程 | **C1(future-work 抽象)、C6(landing object 无界)** | both |
| **6** | **The crossover map**(evaluation) | 一张可证伪的"何处可编程内核解析获胜"操作区域图 | 用"在哪里**不该**用 namei_ext"换可信度;**结构性强制纳入团队在躲的基线**(composefs、FUSE-BPF、ExtFUSE、overlay redirect_dir) | **C4(设计空间过度宣称)、C12(FUSE strawman)、C13(regime 坍缩)** | both |
| **7** | **View epochs**(mechanism) | O(1) 原子 epoch bump 实现免拷贝、可即时回滚的工作区代际 + walk-原子一致性模型 | 实现主 use case 的定义性操作(checkpoint/rollback),并补回 **online 动态轴**(运行中原子切换/fleet churn 下 map 更新)——正是静态表做不到、从而 justify eBPF 的东西 | **C17(fork/rollback 输)、C19(确定性 trace 抹掉动态性)** | both |
| **8** | **Bounded blast radius:对 FUSE 的 fault-containment thesis**(theory-safety)—**门禁失败下仍成立的正向命题** | 故障策略最坏只能 fail-closed 或 sibling-misname;FUSE daemon 却能 stall 内核、reclaim 死锁、阻塞 umount | 门禁失败就**别卖速度,卖故障语义**:证明 namei_ext 的故障权限被界定在 {fail-closed, sibling-misname}(因数据路径原生、static branch 保 no-policy 路径、verifier 界定每次调用终止) | **C9/C11/C15(无 near-native 正向)做柔术** | both |
| **9** | **Authority-preserving resolution**(theory-safety) | 分离 authority(可证保留、native 可达性子集)与 name-integrity(设计上破坏),给 faithfulness 定理 + name-integrity 威胁模型 | 把"内核保留全部权限检查"从 over-broad 断言升级为**精确定理**(inode DAC 保留、可达性 ⊆ native),并诚实枚举新风险(audit/AppArmor/execve//proc desync) | **C7(audit/path-MAC 解耦)、C6(same-parent 假象)** | both |

### C.2 被剔除的方向(为何 cut)

- **#10 coherence calculus / #11 versioned MVCC view / #13 path-resolution algebra / #14 naming type-system**:与 #2/#7 重复计分,或为一周期内可能**不可判定**的 PL-理论(map-driven 程序的跨入口确定性),其有用内核已被 #2(构造性)更廉价地交付。
- **#12 differential view-consistency oracle**:**不作为独立贡献,但要建**——它是 #2 的验证 harness(跨 lookup/readdir/openat/statx/execve//proc 判定一致性,能当场暴露团队自己的不连贯 flagship 策略),便宜、可复现。
- **#15 resolution-time hermeticity(自动最小依赖发现)**:合法但会场漂向 build-systems,且是 hedge 非 lead——仅作"agent fanout 数字不够看"时的明确 fallback。
- **#16 `*_ext` 设计原则泛化(加第二个 readahead_ext 实例)**:**最清楚要 cut**——在第一个实例还没过自己门禁、无宏观证据、无 cross-dir 代码时就泛化成"设计原则 + 第二实例",是典型 vision-paper 倒因为果。

### C.3 两个会决定成败的盲点

1. **composefs 零提及**:composefs(EROFS+fs-verity+overlay redirect)做**已校验的内容寻址内核解析**,整合度强于 namei_ext,且团队材料**完全没提**。**任何把 RIT 包装成 content-addressed/CAS 的做法都是负债**(正撞 composefs);把 RIT 改框成 **per-task versioned 间接**(静态、per-mount、只读的 composefs 做不到),并把 composefs 放进 related work。**content-verified-cache use case(W4)应放弃。**
2. **`namespace_sem` 论证团队从未用过**:这是团队**自己最强的论据**却没说出口;churn 悬崖是独立可查的内核行为,给出真实可测基线。

### C.4 可行性分叉(重要)

- **Maximal paper(≈两周期内核工作)**:D1(lead,含 per-thread)→ 由 RIT(D4)+ 跨目录证书(D5)+ View-epoch(D7)组成的**一个三面机制** → D2 构造性一致 → D3 RCU-safe → D6 crossover map → D8 fault-containment → D9 authority 定理。**对一个目前用 4 字节策略都过不了自己门禁、无 cross-dir 代码、`results/` 为空的团队,风险高。**
- **诚实的一周期安全 paper(推荐先做)**:同样 lead **D1(收窄到 per-PROCESS fanout,per-thread 作为 upside)** + **D2(构造性一致)** + **D3(RCU-safe PASS,几乎必需)** + **D6(crossover map,单独就逼出 composefs 基线)** + **D8(fault-containment 作为门禁失败下仍成立的正向命题)**。**仅当一个可工作的 cross-directory+epoch 切片落地,才把 D4/D5/D7 纳入。** 这套已能中和 subsumption、coherence、design-space、no-positive-result 四组质疑。

---

## 附:把质疑映射到方向(给作者的"防守对照表")

| 质疑 | 由哪个方向消解 |
|---|---|
| C1/C4 抽象 subsume / future-work | D1(竞品-impossible 能力)+ D4/D5(真正 cross-dir 抽象) |
| C2 cgroup_id dead / 无 parent 身份 | D4(return-by-id, per-cgroup view state) |
| C3 常量策略 / 类比夸大 | D4/D7(map-driven、view_id/epoch 真可编程) |
| C5/C10 readdir 不连贯 + O(entries) | **D2(构造性一致,折叠 per-dirent)** |
| C6 same-parent 假象 | D5(landing object 校验 + 证书)+ D9 |
| C7 audit/path-MAC 解耦 | D9(authority/name-integrity 定理 + 威胁模型) |
| C8 O_CREAT 幂等写 | 工程修复(非研究点);可在 D5 写语义里处理 |
| C9 RCU 税 | **D3(RCU-safe BPF)** |
| C11/C15/C17 无正向 / fork 输 | **D1 + D8(fault-containment)+ D7(epoch 实现 rollback)** |
| C12/C13/C14 基线不公/regime/smoke | **D6(crossover map 逼出强基线 + 发布级方法)** |
| C16 Amdahl | D1(证明 setup 在超大 fanout 主导)+ D6(标明何处不该用) |
| C18 sandbox 掏空 | 改框架:lead "per-task path-view 原语",不 lead "sandbox";D8 给出可兑现的故障语义命题 |
| C19 确定性 trace 抹动态性 | **D7(运行中原子切换/fleet churn 这种 live 轴)** |
| C20 更强动机在数据里 | 备选 thesis:per-workload fixture/canary 替换(若 D1 数字不足时的 fallback) |

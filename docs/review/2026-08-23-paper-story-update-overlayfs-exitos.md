# 2026-08-23 论文主张更新：OverlayFS 与 Exitos

本文件记录最近两轮论文讨论的结论，供 PR #2 的合作者审阅。它不改变内核实现，只收紧论文主张和后续实验要求。

## 1. OverlayFS 定位：不能再声称 `namei_ext` 普遍优于 OverlayFS

当前仓库的七个 RQ1 工作流都可以证明已有对象的选择、隐藏、撤销或切换，但这不等于 OverlayFS 做不到。对于需要 COW、写隔离、rollback、commit、whiteout 或目录合并的工作负载，OverlayFS 的能力明显更完整；对于静态的单对象绑定，bind mount 也已经足够。

宿主机 mount 基线还显示，在 1--1000 个 bind-mount view 范围内，扣除控制进程后每个 view 约占 11--13 KB slab，1000 个 view 时没有测出额外的供给延迟。因此论文删除“mount 很贵”“mount 不能提供 per-task view”“mount 不能在运行中改变答案”这三类动机；原始实验计划也明确要求在该结果下撤掉 per-view cost advantage。参见 [`docs/tmp/2026-08-05-rq2-view-supply-cost-experiment-plan.md`](../tmp/2026-08-05-rq2-view-supply-cost-experiment-plan.md) 和本分支的 OverlayFS 定位复核文档。

`namei_ext` 仅剩一个条件化差异：在同一个共享 mount namespace 中，控制器可以按 cgroup 在 lookup/readdir 时选择预注册的原生对象，并通过策略更新改变后续 lookup 的结果，而不重新建立 mount 拓扑。这个差异不是 OverlayFS 的能力缺失，也不是已证明的成本优势；如果任务可以接受独立 mount namespace、视图变化不频繁，并且需要 COW/写隔离，OverlayFS 或 bind mount 应当是默认方案。

因此当前论文可以主张的是：`namei_ext` 是“任务级已有对象绑定”的另一种窄控制面；相对 feature-equivalent FUSE，它在部分 lookup、open、readdir 和 lifecycle 操作上有局部收益。当前 FxMark 结果支持 cache-hot SELECT/FUSE 约 1.052--1.088 的吞吐比，以及大多数 readdir 条件下 2.20--3.66 的吞吐比，但 active policy 仍带来约 6.9--10.5% 的热路径税，且没有 OverlayFS 对照结果。

## 2. Exitos 定位：不加入当前论文

`namei_ext` 处理的是 `pathname -> existing struct path`；Exitos 处理的是 `file offset -> device LBA`。前者在 VFS lookup/readdir 中选择对象后继续使用 lower filesystem 的权限、缓存和读写语义，后者把写路径绕过大部分文件系统并直接提交 raw block I/O。把 Exitos 接入当前论文会引入 extent 生命周期、DMA 完成、页缓存一致性、设备身份和 direct-I/O 安全边界，反而破坏 `namei_ext` 的窄 ownership boundary。

Exitos 当前代码也没有内核租约：`maco_invalidate()` 是用户态前端在 truncate/log rotation 等路径上主动调用的失效操作；`namei_ext` 代码中没有 lease、block、NVMe、io_uring 或 Exitos 接口。Exitos 的 2.1x 数字来自绕过大部分内核存储栈，不能作为 `namei_ext` 的性能证据；8 KiB 直通路径的 WRITE 机制也尚未闭环。

因此 Exitos 在本篇最多作为 related work/limitation：它说明系统还存在更低层的 offset-to-block translation，但本文有意只解决 pathname binding。真正可形成下一篇论文的方向是“面向本地用户态 direct-I/O holder 的可撤销块映射租约”，但它需要在 ext4/iomap 的块释放和重分配入口实现内核主动撤销，排空 in-flight DMA，并覆盖 truncate、fallocate、defrag、reflink、mmap、AIO、io_uring、fork 和 fd 传递；这不是当前 `namei_ext` 的增量扩展。

## 3. 当前可保留的三句主张

Agent 工作区、Bazel 沙箱和 HPC staging 往往已经准备好了目标文件，剩下的子问题只是让每个任务的固定路径指向自己的那份对象；现有方案要么物化一套 namespace，要么为这项绑定再包装一个 FUSE 或 stackable 文件系统。

`namei_ext` 让控制器预注册这些原生对象，再由 cgroup eBPF 在 lookup 和 readdir 时选择或隐藏它们，VFS 验证结果后把权限、页缓存和读写继续交给底层文件系统。

所以论文要证明的不是“mount 很贵”，而是“已有对象的动态视图不必成为一个文件系统”：它相对 FUSE 优化路径查找和目录枚举，视图更新只改变后续 lookup；静态视图仍用 bind mount，写隔离、COW 和 commit 仍由完整文件系统负责。

## 4. 必须补的决定性实验

以 W3 Bazel declared-input view 为主场景，在同一批预先存在的对象和同一组 action 上比较：每 action 一个 mount namespace 加 bind/OverlayFS、symlink forest、cached/passthrough FUSE，以及 `namei_ext`。测量 view setup、运行中 target 更新到 fresh lookup 可见的 p50/p99、action setup/执行时间、控制器 CPU/内存、路径身份、未声明输入泄漏和故障影响范围。

如果 `namei_ext` 在这个同语义比较中既没有更好的动态控制指标，也没有更窄的故障范围，那么它不应被描述为 OverlayFS 的替代品；如果只在“同一 namespace、多 cgroup、运行中更新、已有只读对象”这个窄条件下胜出，论文必须把主张严格限定在这个条件上。

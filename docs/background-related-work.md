# Background And Related Work

Last updated: 2026-07-25
Source/command: `research-literature-novelty` during BOOTSTRAP step
`docs/tmp/bootstrap/step-0002-20260713T004618-0700/`; local PDF corpus in
`docs/reference/`; source catalog in `docs/reference/CODE_SOURCES.md`; current
web checks against primary docs, proceedings pages, official repositories, and
benchmark pages. 2026-07-23 standalone refresh:
`docs/tmp/research-literature-novelty-20260722T172955-0700/2026-07-23-search-report.md`.
Completeness: BOOTSTRAP step
`docs/tmp/bootstrap/step-0005-20260714T174151-0700/` froze the novelty and
evaluation-pressure frontier for BUILD_AND_EVALUATE. This file records source
pressure and baseline obligations; it is not final RQ evidence.

## Search Log

| Date | Query/source | Purpose | Result |
| --- | --- | --- | --- |
| 2026-07-13 | BOOTSTRAP step 0002: Linux `sched_ext`, BPF LSM, FUSE, FUSE passthrough, ExtFUSE, FUSE-BPF, fanotify primary docs/pages | Check same-mechanism and neighboring-hook risk. | `sched_ext` supports the analogy of BPF policy inside a kernel subsystem; BPF LSM and fanotify are security/mediation hooks; FUSE, FUSE passthrough, ExtFUSE, and FUSE-BPF remain filesystem/request-path or stacked-filesystem mechanisms. No same VFS name-resolution policy boundary found. |
| 2026-07-13 | Bento, Wrapfs, DeltaFS, IndexFS, TableFS primary papers/pages and local PDFs | Check custom/stackable/metadata-service boundary. | These systems own broader filesystem or metadata-service interfaces. They support RQ3 boundary pressure but should not become main runnable baselines for the path-view claim. |
| 2026-07-13 | BOOTSTRAP step 0002: AgentFS, BranchFS, YoloFS, Sandlock, Mirage, Redis AFS, ToolFS, agent-vfs, OpenHands, SWE-agent/SWE-ReX, SWE-MiniSandbox, AgentCgroup | Check agent/workspace source-system assets. | Strong source assets exist for branch/COW/checkpoint/sandbox/cache-invalidation/whiteout/symlink/workspace oracles. These are workload sources and boundary evidence, not proof that only `namei_ext` works. |
| 2026-07-13 | BOOTSTRAP step 0002: SWE-Factory, MEnvAgent/MEnvData-SWE, SWE-rebench V2, DockSmith, Multi-Docker-Eval | Check executable source-task and build/cache assets. | Strong executable build/test oracles exist. The selected SWE-Factory-Gym Click task now deepens W2 Agent workspace view selection; the other rows remain traditional environment/cache inputs. DockSmith is mainly trajectory/methodology unless concrete evaluator paths are selected. |
| 2026-07-13 | Kubernetes projected volumes, ConfigMaps, Secrets, OverlayFS, mount namespaces | Check service/config and materialized namespace context. | Projected/materialized mechanisms are important background. Service/config should remain conditional unless a lookup-time object-selection oracle is chosen. |
| 2026-07-23 | OSDI'26, NSDI'26, FAST'26, EuroSys'26, OSDI'25, OSDI'22 proceedings and official repositories for Oxbow, DeLFS, vBPF, PeeR, USEC, Xkernel, Murakkab, FalconFS, KRAKENGUARD, CoFS, SpecFS/SYSSPEC, RFUSE, XRP, bpftime/EIM, SwitchFS, MesaFS, DFUSE, SREGym | Refresh latest related work and check direct same-claim risk. | The new work strengthens the current positioning. Recent systems either optimize FUSE/full filesystems, build distributed/custom filesystems, extend eBPF/kernel programmability, or motivate agentic/system workloads. No primary source found a narrow BPF-controlled VFS name-resolution policy boundary that selects existing lower objects while leaving lower-FS semantics owned by the kernel/lower filesystem. |
| 2026-07-25 | Industrial demand survey: E2B/Modal/Daytona/AgentFS product docs and issues, DeltaBox, kubelet AtomicWriter source, Kubernetes ConfigMap docs, Vault Agent, Reloader, ingress-nginx docs, DMTCP papers, CRIU/Kubernetes KEP-2008, ccache/sccache/Bazel/BuildKit/GitHub Actions cache docs and cache-poisoning incidents, s3fs/gcsfuse/JuiceFS, nydus erofs+fscache migration | Ground each use case in citable industrial demand and check whether lookup-time object selection is the crux behavior. | Full record: `docs/tmp/2026-07-25-usecase-industrial-demand-survey.md`. Six domains re-implemented lookup-time object selection at wrong layers (FUSE, stackable FS, symlink hack, embedded Lua, LD_PRELOAD). YoloFS independently implements the HIDE/REDIRECT/SELECT semantics; kubelet's config switch already lives at name resolution; nydus's FUSE→erofs+fscache migration is the strongest industrial proof of the boundary. Industry's mainstream cache isolation is key-space, so build/cache is positioned as access-point view governance, not acceleration. |
| 2026-07-25 | XDG Documents portal, Bazel sandboxfs, Nix/Guix/Spack/venv/Conda/nvm/rbenv/Lmod/update-alternatives, CernVM-FS variant symlinks, LLNL Spindle, GNU Hurd `stowfs`, systemd-sysext/confext, NVIDIA Container Toolkit, glibc dynamic-linker docs; FxMark, IOR/mdtest, Filebench | Expand traditional case studies, distinguish source workloads from performance benchmarks, and find a concrete remote/shared-cache source system. | XDG grants and Bazel action views provide exact source oracles. Toolchain systems form one W7 profile/view family, not many weak cases. CernVM-FS implements configuration-dependent path selection inside FUSE. Spindle is the strongest new traditional case: production LLNL software redirects libraries/executables/Python from a shared filesystem to prepared node-local objects through loader/libc interposition. FxMark is the primary standard VFS RQ2 benchmark; mdtest and Filebench provide secondary breadth. Full record: `docs/tmp/2026-07-25-case-study-and-standard-benchmark-plan.md`. |

## PDF Corpus

| Work | Local PDF path | Verification status | Why kept |
| --- | --- | --- | --- |
| BranchFS / branch contexts | `docs/reference/arxiv2602.08199-branch-contexts-branchfs.pdf` | local PDF plus public repository/source checks | Agent workspace branch/COW lifecycle source and FUSE boundary evidence. |
| YoloFS (SOSP 2026) | `docs/reference/arxiv2604.13536-yolofs.pdf` | local PDF plus public filesystem artifact checks | Agent filesystem safety methodology, staging/snapshot/permission oracles, stackable-FS context. |
| Sandlock | `docs/reference/arxiv2605.26298-sandlock.pdf` | local PDF plus public repository/source checks | Agent sandbox and reversible filesystem effects source. |
| SWE-agent/OpenHands/Terminal-Bench/AgentCgroup/SWE-MiniSandbox | `docs/reference/arxiv2511.03690-openhands-sdk.pdf`, `docs/reference/neurips2024-yang-swe-agent.pdf`, `docs/reference/arxiv2601.11868-terminal-bench.pdf`, `docs/reference/arxiv2602.09345-agentcgroup.pdf`, `docs/reference/arxiv2602.11210-swe-minisandbox.pdf` | local PDFs plus source reproduction records | Agent runtime/task/workspace oracles and operation traces. |
| MEnvAgent, SWE-rebench V2, DockSmith, Multi-Docker-Eval | `docs/reference/arxiv2601.22859-menvagent.pdf`, `docs/reference/arxiv2602.23866-swe-rebench-v2.pdf`, `docs/reference/arxiv2602.00592-docksmith.pdf`, `docs/reference/arxiv2512.06915-multi-docker-eval.pdf` | local PDFs plus source reproduction records | Traditional build/cache oracle sources. |
| Software profile and version views | `docs/reference/lisa04-dolstra-nix.pdf`, `docs/reference/els13-courtes-guix.pdf`, `docs/reference/sc15-gamblin-spack.pdf` | local PDFs plus current official manuals and repositories | W7 source evidence for installed variants, symlink/profile views, per-user generations, rollback, and real HPC toolchain stacks. |
| HPC shared-to-local relocation | `docs/reference/ics13-frings-spindle.pdf` | local paper, current LLNL production docs, and source inspection at commit `8853636d2d774729a5a728f5cf6c296b65a1099c` | W6 source behavior and oracle for redirecting libraries, executables, Python, and selected data files to prepared local objects. |
| Standard VFS benchmark | `docs/reference/atc16-min-fxmark.pdf` | local paper and source inspection at commit `3f29552ce7ba6be24c4172e6e2c2c1f603209953` | Primary RQ2 benchmark for pathname resolution and directory iteration; not RQ1 case-study evidence. |
| FUSE/ExtFUSE/Bento/Wrapfs | `docs/reference/fast17-vangoor-to-fuse-or-not-to-fuse.pdf`, `docs/reference/atc19-bijlani-extfuse.pdf`, `docs/reference/fast21-miller-bento.pdf`, `docs/reference/usenix99-zadok-wrapfs-stackable-templates.pdf` | local PDFs plus primary pages | RQ2 and RQ3 comparison families. |
| DeltaFS/IndexFS/TableFS | `docs/reference/sc21-zheng-deltafs.pdf`, `docs/reference/sc14-ren-indexfs.pdf`, `docs/reference/atc13-ren-tablefs.pdf` | local PDFs plus primary pages | Related-work boundary for metadata services and stacked metadata filesystems. |
| Recent FUSE and FUSE-like optimizations | `docs/reference/fast24-cho-rfuse.pdf`, `docs/reference/fast26-wang-cofs.pdf`, `docs/reference/arxiv2503.18191-distfuse.pdf` | local PDFs plus primary pages; MesaFS ACM PDF download returned HTTP 403, so MesaFS is tracked by DOI/program metadata only | RQ2 fairness pressure: do not argue against naive FUSE only; compare against feature-equivalent FUSE and cite optimized FUSE systems as the strongest surrounding work. |
| Recent full/custom/distributed filesystem systems | `docs/reference/osdi26-kim-oxbow.pdf`, `docs/reference/osdi26-ahn-delfs.pdf`, `docs/reference/nsdi26-xu-falconfs.pdf`, `docs/reference/fast26-liu-specfs.pdf`, `docs/reference/arxiv2410.08618-switchfs.pdf` | local PDFs plus primary pages/repositories where available | RQ3 boundary pressure: recent strong systems still build full, multi-component, generated, or distributed filesystems when they need metadata/data-path/object-model ownership. |
| Recent eBPF/kernel-extension systems | `docs/reference/osdi26-zhang-vbpf.pdf`, `docs/reference/osdi26-carin-peer.pdf`, `docs/reference/nsdi26-patel-krakenguard.pdf`, `docs/reference/osdi22-zhong-xrp.pdf`, `docs/reference/osdi25-zheng-bpftime-eim.pdf`, `docs/reference/osdi26-chen-xkernel.pdf`, `docs/reference/osdi26-jiang-usec.pdf` | local PDFs plus primary pages/repositories where available | Neighboring programmability and safety work: eBPF virtualization/scheduling/isolation, storage-function hooks, userspace extension interfaces, kernel performance tunability, and access-control frameworks. |
| Recent agentic and data-pipeline context | `docs/reference/osdi26-chaudhry-murakkab.pdf`, `docs/reference/arxiv2605.07161-sregym.pdf`, `docs/reference/osdi26-chen-llm-data-pipeline.pdf`, `docs/reference/osdi26-holmes-spice.pdf` | local PDFs plus primary pages/repositories where available | Workload context for agentic orchestration, live SRE-agent environments, data/cache pipeline pressure, and checkpoint/restore; not direct mechanism baselines. |

## Claim-Oriented Novelty Map

| Claim | Closest prior work | Same-claim risk | Novelty delta | Baselines implied | Expansion opportunity |
| --- | --- | --- | --- | --- | --- |
| A narrow kernel hook can expose programmable path-view policy at VFS name resolution while the lower filesystem keeps data and object semantics. | `sched_ext`, BPF LSM, fanotify, FUSE/ExtFUSE/FUSE-BPF/RFUSE/CoFS/DFUSE, Oxbow/DeLFS/FalconFS/SpecFS/SwitchFS, XRP/vBPF/PeeR/KRAKENGUARD/Xkernel/bpftime/USEC. | Medium. Nearby mechanisms exist, but they are scheduler policy, access control, userspace filesystem request handling, optimized/custom/full filesystem designs, storage-driver hooks, or general kernel/eBPF extension systems. | The proposed boundary is name-resolution object selection/visibility, not security mediation, FUSE request handling, storage-function offload, kernel constant tuning, or filesystem method ownership. | RQ2: feature-equivalent FUSE. RQ3: custom/stackable/full-FS boundary. | Frame as a missing middle between eBPF LSM and FUSE/custom FS, not as "tables fail" or "dynamic policy exists." |
| Independent source systems expose reusable object-selection and visibility policies across Sandboxed Application File Sharing, Agent Workspaces, Build Action Sandboxing, Service Configuration and Secret Rotation, Checkpoint/Restore and Migration, HPC File Staging, and Toolchain and Dependency Environments. | XDG Document Portal, AgentFS/BranchFS/YoloFS, Bazel sandboxfs, Kubernetes AtomicWriter, DMTCP, Spindle, Nix/Guix/Spack/CernVM-FS. | Low-to-medium. Each system implements a broader product, filesystem, runtime, or control plane. | Replay only the existing-object path-view subset and retain the source behavior as the correctness oracle. | Source behavior for RQ1; feature-equivalent FUSE for RQ2; workload-specific boundary accounting for RQ3. | Demonstrate breadth through seven non-overlapping industrial workflows and one common action model, then give deep performance and boundary evidence on representative cases rather than inventing a baseline per source. |
| A verified name-resolution policy can have lower boundary burden than custom or stackable FS when policy is only lookup/readdir selection. | Bento, Wrapfs, TableFS, DeltaFS, IndexFS, Oxbow, DeLFS, FalconFS, SpecFS/SYSSPEC, SwitchFS/MesaFS, YoloFS/BranchFS. | Medium. Prior work values custom/stackable/full/distributed FS for broader semantics. | The novelty is narrower ownership, not replacing full filesystems. | Boundary accounting: methods owned, daemon/state, verifier constraints, invalid-policy containment. | Make RQ3 a same-oracle boundary audit after RQ1 correctness, not a prose-only related-work table. |

## Closest Work

| Work | Claim | Method/artifact | Evaluation | Same problem/mechanism/metric/setting? | Gap relative to this project |
| --- | --- | --- | --- | --- | --- |
| Linux `sched_ext` | BPF can safely define scheduling policy inside a kernel-owned subsystem. | Kernel scheduler class and BPF scheduler programs. | Scheduler workloads. | Same extension-point pattern; different subsystem. | Supports analogy but not filesystem path-view policy. |
| BPF LSM and fanotify | Runtime security/access mediation for files and other objects. | LSM hooks or userspace permission events. | Security/audit decisions. | Same kernel/filesystem neighborhood; different action space. | They decide allow/deny or audit, not which lower object a pathname resolves to. |
| FUSE and FUSE passthrough | Userspace can implement filesystems; passthrough can reduce some I/O daemon cost. | Kernel module plus userspace daemon/library; passthrough backing files. | Filesystem workloads. | Same setting and closest RQ2 mechanism. | FUSE owns a filesystem service boundary; same-oracle measurement is required and must account for fair caching/passthrough choices. |
| ExtFUSE and FUSE-BPF | Kernel-side extensions can accelerate or filter FUSE/stacked-filesystem request handling. | Modified FUSE framework, BPF-like or eBPF request handlers, stacked-filesystem patch series. | FUSE or stacked-filesystem workloads. | Same eBPF/FUSE neighborhood and closest same-mechanism pressure. | Still starts from a FUSE or stacked-filesystem boundary, not a VFS name-resolution-only extension point. |
| RFUSE, CoFS, and DFUSE | Recent systems show FUSE can be substantially optimized or specialized. | Per-core FUSE communication, extended FUSE for fixed container-image lookup/data caching, and distributed FUSE caching/consistency designs. | FAST and arXiv filesystem/container/distributed workloads. | Same RQ2 pressure; not the same mechanism boundary. | Prevents a weak "FUSE is slow" claim. The paper should use feature-equivalent FUSE as the main numerical baseline and cite optimized FUSE systems as related-work pressure unless one is admitted into the final protocol. |
| Bento and Wrapfs | Safer or easier ways to build kernel/stackable filesystems. | Rust kernel FS framework; stackable FS templates. | Filesystem implementations. | Same boundary question. | They still ask the developer to own filesystem methods. |
| DeltaFS, IndexFS, TableFS | Metadata/index services and stacked metadata filesystems improve metadata scalability. | Distributed/stacked metadata systems. | HPC/local metadata workloads. | Same broad filesystem namespace area; different problem. | Useful non-goals and appendix context, not the main path-view workload. |
| Oxbow, DeLFS, FalconFS, SwitchFS, MesaFS, SpecFS/SYSSPEC | Recent filesystem papers build multi-component, kernel, distributed, metadata-service, or generated filesystems for stronger semantics, scalability, or evolution. | Full filesystem/storage architectures; some public code or artifacts exist. | OSDI/NSDI/FAST/EuroSys filesystem workloads. | Same broad filesystem namespace/storage setting; broader problem and mechanism. | Strong RQ3 boundary evidence: when the oracle needs data-path ownership, metadata persistence, distributed metadata, or a new object model, `namei_ext` is not the right boundary. |
| XRP | eBPF can safely run storage functions near NVMe and preserve filesystem semantics with propagated state. | eBPF hook in NVMe driver plus application storage functions. | Key-value store workloads. | Same eBPF/storage performance theme; different hook and action space. | Closest "BPF for storage" precedent, but it is data-path/storage-function offload rather than VFS name-resolution object selection. |
| vBPF, PeeR, KRAKENGUARD, Xkernel, bpftime/EIM | Recent systems improve eBPF/kernel extension virtualization, scheduling, isolation, runtime tunability, or userspace extension safety. | eBPF virtualization/scheduling/isolation; runtime kernel perf-constant tuning; eBPF-style userspace extension framework. | lmbench/PostgreSQL, Redis/Memcached/TPC-C, XDP-as-a-Service, kernel subsystem case studies, userspace extension use cases. | Same programmable-kernel-extension neighborhood; different subsystem and claim. | Supports the need to state verifier, isolation, tail-latency, and extension-boundary assumptions, but does not solve filesystem path-view selection. |
| USEC | A production MAC framework can simplify and accelerate access-control enforcement. | LSM-compatible access-control extension. | Server/desktop workloads and production deployment. | Same file-access neighborhood; access control not object selection. | Reinforces the distinction between allow/deny mediation and choosing which existing lower object a path resolves to. |
| AgentFS, BranchFS, YoloFS, Sandlock, Mirage, Redis AFS, ToolFS, agent-vfs | Agent filesystems/sandboxes need COW, branch, snapshot, staging, permission, and workspace state. | FUSE/NFS/kernel module/runtime/filesystem/library systems. | Agent/workspace tests and benchmarks where available. | Same workload setting; broader mechanisms. | Best source oracles and boundary evidence for Experiment A. |
| XDG Document Portal | Sandboxed applications need dynamic, per-application grants to existing host documents. | FUSE document view plus permission database and portal service. | Grant/revoke and per-application visibility tests. | Same W1 policy setting and a broader source implementation. | The current `namei_ext` case covers existing-object visibility/selection only, not synthetic document hierarchy, portal UI, persistent grants, or mode synthesis. |
| Bazel sandboxfs | Build actions need arbitrary declared-input views without constructing large symlink forests. | FUSE view configured with hundreds or thousands of mappings per action. | Bazel action builds and sandboxfs benchmarks. | Same W3 path-view problem and the main source implementation. | `namei_ext` tests existing local targets and hide/select behavior; remote execution, CAS transfer, output handling, and process isolation remain outside the boundary. |
| Spindle | Large HPC jobs need libraries, executables, Python files, and selected data redirected from shared storage to prepared node-local copies. | Dynamic-loader and libc interposition plus a distributed cache/broadcast controller. | Pynamic and production LLNL launches; public testsuite. | Same W6 object-selection problem, different interception mechanism and a broader cache control plane. | `namei_ext` can replace only final pathname selection; Spindle still owns distribution, population, object recognition, and cache sessions. |
| Nix, Guix, Spack, Lmod, CernVM-FS variant symlinks | Users and HPC jobs need concurrent software variants, profile switching, rollback, and site-specific software views. | Store paths plus profile/symlink trees, environment/PATH manipulation, or configuration-expanded symlinks inside FUSE. | Package-manager/profile operations and real toolchain/application checks. | Same W7 setting; mechanisms range from materialized views to FUSE policy. | The case tests a common installed-object selection boundary and does not replace solving, installation, activation scripts, or ABI management. GNU Hurd `stowfs` is a prior dynamic-filesystem profile precedent. |
| Murakkab and SREGym | Agentic systems and benchmarks increasingly expose real multi-step tool/runtime environments. | Agentic workflow serving and live SRE-agent benchmark code. | OSDI/arXiv agentic workloads. | Same broad agentic setting; not a filesystem mechanism. | Useful motivation for real source environments and oracles, but not a main filesystem baseline. |
| SWE-Factory, MEnvAgent, SWE-rebench V2, DockSmith | Environment construction and reuse provide real repository build/test oracles. | Docker/eval pipelines, datasets, trajectories. | Fail-to-pass and build/test oracles. | Same executable-task setting; different mechanism. | The released SWE-Factory-Gym Click task deepens W2 Agent workspace view selection. The remaining sources are traditional environment/cache inputs rather than filesystem baselines. |

## Industrial Demand Evidence For Use Cases

Added 2026-07-25. Full record with verbatim quotes and URLs:
`docs/tmp/2026-07-25-usecase-industrial-demand-survey.md`. The framing
result from the first pass was six domains. The second pass expanded the
formal portfolio with Sandboxed Application File Sharing, Build Action
Sandboxing, HPC File Staging, and
toolchain/profile systems; its record is
`docs/tmp/2026-07-25-case-study-and-standard-benchmark-plan.md`. Each row below
states the demand it proves, the mechanism used today, and the role the source
plays for this paper.

### Agent workspace views

| Source | Demand proven | Mechanism today and documented pain | Role for the paper |
| --- | --- | --- | --- |
| YoloFS (SOSP 2026; arXiv:2604.13536, UW-Madison) | From 290 real incident reports: "AI coding agents operate directly on users' filesystems, where they regularly corrupt data, delete files, and leak secrets." | A full stackable kernel filesystem; its `Hidden` state makes "readdir skip it, and lookup, stat, and open behave as if it does not exist"; override-tree lookup chain selects staged/base/tombstone objects. OverlayFS rejected as "too expensive for agent workloads". | Strongest alignment evidence: an independent serious system implements our HIDE/REDIRECT/SELECT semantics and pays an entire kernel filesystem for them. Cite for demand, action-set validity, and RQ3 boundary cost. |
| BranchFS (arXiv:2602.08199) | "Current agent frameworks resort to ad hoc solutions such as git stashing, temporary directories, or container clones, which incur significant overhead." R5 rules out "heavyweight mechanisms such as VM snapshots, privileged container runtimes, or filesystem-specific solutions." | FUSE branch filesystem: "regular FUSE mode achieves 1,655 MB/s read throughput (19% of native)". | Cite the six workspace requirements and the FUSE overhead number as direct RQ2 motivation. |
| Turso AgentFS (product blog, docs, issues #228/#167) | "A POSIX filesystem gives infinite tool compatibility with zero integration effort" — agents must run unmodified Unix toolchains. | FUSE + SQLite overlay. Issue #228: `make -j10` kernel build "about 10X slower in agentfs" from FUSE serialization. Issue #167: daemon deadlock; user reports OverlayFS unusable with autofs in the same issue. | First-hand industry evidence that the FUSE route hurts at exactly the metadata-heavy workloads; supports RQ2 fairness framing and the daemon-reliability argument. |
| DeltaBox (arXiv:2605.22781) plus E2B/Modal/Daytona product docs | Agent sandbox checkpoint/rollback is a product-level requirement (E2B pause: "approximately 4 seconds per 1 GiB of RAM"; Modal ships directory snapshots). | Whole-VM or whole-image granularity: "hundreds of milliseconds to seconds of latency per C/R"; standard overlayfs "freezes its layer stack at mount time" so DeltaBox had to patch the kernel module. | Demand evidence for fan-out/branch workloads; also shows even kernel-adjacent teams hit the missing middle and patch around it. |
| Sandlock (arXiv:2605.26298) | "Containers and microVMs add privilege, image-management, and startup costs, while ad-hoc process controls... provide weak guarantees." | Unprivileged process sandbox (seccomp-style policy compilation), not a name-resolution mechanism. | Breadth demand citation only. |

Scope note: server-side multi-tenant sandboxes (E2B/Modal) have settled on
microVMs; the home ground for namei_ext is local coding agents and single-VM
branch fan-out. namei_ext covers the view/visibility half of the agent
requirement, not write-path COW staging.

### Build/cache view governance

| Source | Demand proven | Mechanism today and documented pain | Role for the paper |
| --- | --- | --- | --- |
| sccache (Mozilla README) | Shared compile caches are default production form: "Multi-level caching with automatic backfill is supported for hierarchical cache architectures"; ten storage backends. | Compiler wrapper; requires absolute-path matching for hits, with `SCCACHE_BASEDIRS` to normalize paths — cache semantics coupled to path representation. | Establishes deployment scale; the path-coupling detail supports "cache visibility is a path problem at consumption time". |
| ccache manual (`namespace`, `secondary_storage`, `hard_link`) | "A group of developers can increase the cache hit rate by sharing a cache directory"; `namespace` gives logical views over one physical cache. | Isolation is key-space (namespace mixed into hash). `hard_link` trap: multiple build trees sharing one inode leak mtime updates and corrupt cache entries. | Shows per-workspace cache views are a recognized need; the hard_link trap is a concrete correctness failure of "same pathname, same shared object across workspaces". |
| Bazel remote caching docs + issue #4276 | Content-addressed action/output caches shared across machines; disk cache shared "when switching branches and/or working on multiple workspaces". | "Take care in who has the ability to write to the remote cache"; issue #4276: "the cache will be poisoned. We've seen this in production." Poisoned caches cannot be cleaned per-entry. | Production-grade evidence that cache object selection/visibility needs a policy layer with trust differentiation. |
| BuildKit `RUN --mount=type=cache` | The `id` parameter selects which cache backs a given target path; builds "should work with any contents of the cache directory". | Cache view selected at mount time per build step; `id` defaults are themselves disputed (moby/buildkit#1706). | Productized precedent for "same path, identity-dependent backing object" — the closest industrial analogue of the namei_ext action set. |
| GitHub Actions cache docs + 2026-06 read-only token change + Angular incident (Adnan Khan) | "Branches are the security boundary for GitHub Actions caching"; cache is immutable per key, versioned, evicted by policy. | Cache poisoning escalated into a supply-chain compromise of Angular; GitHub now issues read-only cache tokens to untrusted triggers. | Strongest evidence that "who may see/write which cache view" must be decided per caller context; today only platforms can enforce it. |

Honest verdict recorded in the survey: industry's mainstream isolation is
key-space, deliberately bypassing paths; no primary source asks for a kernel
path hook. The defensible gap is cache visibility/writability governance for
unmodified toolchains on shared build machines. Position the use case as
access-point governance, not ccache acceleration.

### Service/config rotation

| Source | Demand proven | Mechanism today and documented pain | Role for the paper |
| --- | --- | --- | --- |
| kubelet AtomicWriter (source comment) + Kubernetes ConfigMap docs | Config/secret objects must switch generations under a stable path. | The switch already lives at name resolution: visible files are symlinks into "a hidden timestamped directory... atomically updated by changing the target of the data directory symlink". Propagation delay = sync period + cache delay (measured 60–90s); subPath mounts never update; env consumption needs pod restart. | Central evidence that epoch switching *is* a name-resolution behavior implemented as a symlink hack; namei_ext removes the minute-scale reconcile trigger and the choreography. |
| Vault Agent injector/template docs | Rotated secrets must reach running applications. | Sidecar renders the new secret to a shared file and runs an `exec` reload command — the application must cooperate. | Shows the cost of having no lookup-time switch: a whole sidecar/reload ecosystem. |
| stakater Reloader (9k+ stars) | "Kubernetes does not trigger pod restarts when a referenced Secret or ConfigMap is updated." | A standalone controller watches changes and performs rollouts. | Popularity of the workaround is demand evidence. |
| nginx control docs, envoy hot-restart docs | Configuration must switch without dropping the service. | Full process replacement plus drain protocols (envoy: two processes coordinate over UDS RPC). | Process-level switching exists partly because config objects cannot be re-selected at lookup time. |
| ingress-nginx "How it works" | Reloads hurt: "this feature saves significant number of Nginx reloads which can otherwise affect response latency, load balancing quality". | Embeds lua-nginx-module to move upstream/certificate object selection to request time. | The single best citation that industry re-invents lookup-time object selection inside applications when the OS does not offer it. |

Honest boundary: namei_ext cannot help applications holding old file
descriptors; it serves every application that re-resolves paths at open.
Oracle candidates are concrete (`nginx -t`, service-visible behavior across
epochs). Promoted to the third use case on 2026-07-25.

### Checkpoint/restart path remapping

| Source | Demand proven | Mechanism today and documented pain | Role for the paper |
| --- | --- | --- | --- |
| DMTCP path virtualization (IEEE Cluster'16) | "A path virtualization plugin translates paths remembered by the application into correct paths as per the new mount points." | LD_PRELOAD interposition of `open()` — fragile user-space path virtualization. | Direct precedent that lookup-time remapping is the crux behavior; the implementation layer is the problem. |
| DMTCP × Intel (SELSE'17, arXiv:1703.00897) | Real industrial migration scenario: "the environment variables, the file paths, and the files that are saved as part of a checkpoint image make such migrations challenging." | Same plugin approach; interposes `open` for filename virtualization. | Industrial reality of the demand. |
| CRIU `--external mnt`/`--inherit-fd` + KEP-2008 | Restore may run "at a later time, on a different system, or both". | Static, operator-supplied mapping at restore start; Kubernetes keeps migration a non-goal (Beta, forensic-only, in-place restore). | Bounds W5 to moved-root restart and reopen; broader migration and checkpoint ownership remain with DMTCP or CRIU. |

### W6 HPC File Staging

| Source | Demand proven | Mechanism today | Role for the paper |
| --- | --- | --- | --- |
| LLNL Spindle paper, public source, and El Capitan user guide | At large process counts, repeated library search/load operations overwhelm a shared filesystem and delay application startup; current LLNL systems enable Spindle automatically. | A distributed staging/cache controller plus dynamic-loader and libc interposition for `open`, `stat`, `exec`, libraries, Python, and configured data prefixes. | Formal W6 source. Keep distribution/cache population in Spindle and test whether `namei_ext` can replace only final shared-versus-local pathname selection. |

This source is stronger than a generic remote-cache scenario because the
public implementation and production guide expose an exact path-redirection
boundary and executable workload. It also makes the limitation explicit:
`namei_ext` is not the cache or broadcast system.

### Toolchain and dependency views

| Source | Demand proven | Mechanism today | Role for the paper |
| --- | --- | --- | --- |
| Nix and Guix | Multiple variants, per-user profiles, transactional generation switches, and rollback | Immutable store paths plus symlink profile trees and environment search paths | W7 profile generation/switch/rollback source behavior |
| Spack and Lmod | Production HPC applications require many compiler, MPI, dependency, and architecture variants | Spack materializes linked environment views; Lmod modifies environment and search paths | W7 real toolchain and build/import workflow |
| Python venv, Conda, nvm, rbenv, update-alternatives | Project/user/system version selection is widespread outside HPC | Environment prefixes, `PATH`, shims, activation, and symlink groups | Breadth and alternative source workflows within W7 |
| CernVM-FS variant symlinks | A global software-distribution filesystem needs site-configurable software and certificate targets | FUSE expands configuration variables in a symlink at access time | Direct evidence that a production FUSE filesystem embeds this path-selection policy |

GNU Hurd `stowfs` is an older dynamic-filesystem profile precedent recorded by
the Guix paper. systemd-sysext/confext is a current OverlayFS materialization
mechanism for read-only `/usr`, `/opt`, and `/etc` extensions. Both belong in
related work and RQ3 accounting; neither needs a separate benchmark.

### Remote filesystem cache (motivation evidence, not evaluated)

| Source | Demand proven | Mechanism today and documented pain | Role for the paper |
| --- | --- | --- | --- |
| s3fs/gcsfuse/JuiceFS/Alluxio | Mounting cloud object storage as a filesystem is a widespread need. | All are FUSE daemons with documented performance/consistency pain (gcsfuse semantics docs; JuiceFS exists as a company on this gap). | Background demand for daemon-free remote views. |
| nydus evolution (d7y.io blog), AWS EKS AMI issue #2569, community lazy-pull analysis | Lazy container-image pulling is production-critical. | Migrated from FUSE to in-kernel erofs+fscache/cachefiles on-demand (Linux 5.19+): "in-kernel fscache mode that provides significantly better performance than FUSE"; "the only solution that eliminates FUSE from the data path entirely". | The strongest industrial proof of the namei_ext boundary: the kernel absorbed the data path, but selection/visibility policy (which object, which generation, when a cached entry is stale/hidden) remains hardwired per system. |

Not an evaluated use case: the data path (fetch, prefetch, consistency) is
the bulk of the problem and namei_ext deliberately does not own it; a fair
RQ2 comparison would require building a fetcher for both sides. If promoted
later, the shape is namei_ext plus an existing fetcher (e.g. fscache) for
per-workload remote-cache view governance.

### Additional source family and existing-mechanism precedents

| Source | What it proves | Static view or lookup-time policy? | Role for the paper |
| --- | --- | --- | --- |
| SELinux polyinstantiation / pam_namespace (namespace.conf(5), Red Hat SELinux guide) | The closest shipping mechanism for per-context divergent views: `/tmp` polyinstantiated "based on user name, sensitivity level or complete security context". | Static: per-context instance trees bound at login; not programmable, no redirect/hide/verified-selection semantics. | Must-cite precedent. Position: namei_ext generalizes a static, login-time, directory-granularity mechanism into a programmable per-lookup policy. |
| Plan 9 per-process name spaces and union directories (Pike et al., Operating Systems Review 27(2):72–76, 1993) | The intellectual ancestor: each process assembles its own view; union directories merge file trees at one point. | Static namespace construction per process; Linux inherited it as mount namespaces. | Historical grounding for the mechanism ladder; the delta is policy evaluated at lookup inside one shared namespace. |
| Nix/Guix/Spack and the virtualenv/Conda/nvm/rbenv/Lmod/update-alternatives ecosystem | Simultaneous compiler, MPI, package, and dependency variants are a production requirement; users need per-project/per-job selection, profile generations, and rollback. | Store paths plus symlink/profile trees, environment/PATH manipulation, or generated environment views. CernVM-FS variant symlinks put configuration-dependent target selection inside its FUSE filesystem. | Formal W7 source family. Reuse installed objects and source-native version/build/import oracles; keep solving, install/build, activation, and ABI management out of scope. |
| lakeFS / DVC | Branch/checkout views are wanted for data lakes, not just code ("Git-like semantics... on top of an existing data lake"). | Object-store metadata layer consumed via S3 APIs/SDKs, not POSIX paths. | Related work only; does not exercise a VFS boundary. |

No strong primary citation was found for honeypot/decoy filesystems in this
pass; the scenario matches HIDE/REDIRECT semantics but is not recorded as
evidence.

## Main Comparisons And Evidence Roles

This section separates baselines from oracles, controls, and boundary evidence
so the evaluation does not drift into a long baseline catalog.

| Role | Evidence item | RQ served | Runnable status | Fairness or admission rule | Claim consequence if unavailable |
| --- | --- | --- | --- | --- | --- |
| Main baseline | Feature-equivalent FUSE policy over the same oracle | RQ2 | Formal Agent lifecycle and FxMark lookup/readdir matrices completed; workload-specific breadth remains possible | Same policy inputs, update schedule, and justified FUSE caching/passthrough settings as `namei_ext`; account for FUSE passthrough, FUSE-BPF, RFUSE, CoFS, and DFUSE as related acceleration context. | RQ2 cannot claim lower cost or acceptable overhead versus FUSE. |
| Correctness oracle | Source/native behavior from XDG portal, AgentFS/BranchFS/YoloFS, Bazel sandboxfs, Kubernetes AtomicWriter, DMTCP, Spindle, and selected toolchain/profile systems | RQ1 | All seven industrial workflows have reviewed formal KVM results, including W6 source Spindle, direct `namei_ext`, and withdrawn-target conditions over 47 source-produced cache objects | Establishes the source behavior and task input; it is not a weaker baseline. | RQ1 lacks source credibility. |
| Boundary evidence | Workload-specific custom/stackable/source-system ownership table | RQ3 | Citation/source-code evidence plus selected source artifacts; no full-system reimplementation unless required by the oracle | Compare required filesystem methods, daemon/runtime state, metadata, data/write-path ownership, privileged code, and invalid-policy containment. | RQ3 becomes unsupported prose. |
| Control | Lower-FS/no-hook run through the project KVM target | RQ2 attribution | Existing Phase 1 controls; final workload controls pending | Same operation mix where meaningful; used only for overhead attribution. | RQ2 overhead attribution weakens. |

## Experimental Precedents And External Assets

| RQ/claim | Accepted paper/protocol citation | Official benchmark/dataset/software/test tool | Version/artifact | Real-world provenance | Reusable design | Required deviation or glue |
| --- | --- | --- | --- | --- | --- | --- |
| RQ1 Agent workspace | AgentFS/BranchFS/YoloFS/Sandlock/Mirage source systems plus SWE-Factory-Gym | AgentFS lifecycle tests and released Click task `pallets__click-2622` | See `docs/reference/CODE_SOURCES.md` | AI agent workspace filesystems, sandboxes, and executable SWE tasks | Branch/stage/hide/whiteout oracles plus exact fail-to-pass task outcome | Thin KVM glue maps only existing-object workspace selection to `namei_ext`; COW, patch generation, and evaluator orchestration remain outside. |
| RQ1 Traditional path views | XDG Documents portal, Bazel sandboxfs, Kubernetes AtomicWriter, DMTCP, Spindle, Nix/Guix/Spack/CernVM-FS | Official source repositories, APIs, tests, and real command oracles | See `docs/reference/CODE_SOURCES.md` and `docs/tmp/2026-07-25-case-study-and-standard-benchmark-plan.md` | Desktop sandboxing, build actions, service operation, restart, HPC launch, and software environments | Grant/revoke, action input views, epoch switch, path remap, shared-to-local relocation, profile switch/rollback | Each family now has a reviewed three-boot KVM source-oracle result; deeper performance work remains representative rather than per-family. |
| RQ2 Standard VFS cost | FxMark ATC'16, IOR/mdtest, Filebench | FxMark `MRPL/MRPM/MRPH/MRDL/MRDM`, selected mdtest operations, Filebench fileserver/webserver | Checked-out source commits in the dated plan | Standard pathname, directory, metadata, and mixed filesystem operations | Stock versus patched-unattached versus attached `PASS`/`SELECT` versus FUSE | Add Make-owned KVM runners and preserve per-run raw throughput, latency, CPU, and perf counters. |
| RQ2 Existing ccache macro | ccache plus Redis/nginx compile workload | Current KVM compile matrix | `results/experiments/build-cache/` and `results/phase1/` | Real compile output and cache traffic | Retain as representative macro timing evidence | Do not use ccache itself as the headline source-system motivation; add independent-run statistics and hardened FUSE settings. |
| RQ2 FUSE overhead | FAST 2017 FUSE study; ExtFUSE; FUSE-BPF; RFUSE; CoFS; DFUSE; kernel FUSE and FUSE passthrough docs | libfuse/project FUSE runner | Local runner plus official FUSE docs and closest-work records | Filesystem request path | Measure same-oracle FUSE, not generic FUSE weakness | Implement feature-equivalent policy cells and explain why FUSE-BPF/ExtFUSE/RFUSE/CoFS/DFUSE remain related-work pressure rather than main baselines unless admitted by the final protocol. |
| RQ3 Boundary | Bento, Wrapfs, ExtFUSE, DeltaFS/IndexFS/TableFS, Oxbow, DeLFS, FalconFS, SwitchFS, MesaFS, SpecFS/SYSSPEC | Papers, repositories, filesystem method surfaces | Local PDFs and source links where downloadable; MesaFS tracked through ACM/EuroSys metadata | Custom/stackable/full/distributed/filesystem-service systems | Account ownership rather than re-run entire FS papers | Produce workload-specific boundary table after final oracle selection. |
| Agentic workload context | Murakkab and SREGym | OSDI paper, arXiv paper, SREGym repository | Local PDFs/source links | Agentic workflows and live operational environments | Use only to justify real source environments and oracles | Do not make these filesystem baselines; they motivate workload realism. |

## Non-Main Comparison Disposition

| Candidate | Disposition | Reproduction risk | Fairness notes |
| --- | --- | --- | --- |
| FUSE policy implementation | Main RQ2 baseline. | Medium. | Must be feature-equivalent and correctness-gated. |
| Source/native behavior | Correctness oracle and input provenance, not a baseline win condition. | Medium. | Establish source behavior before interpreting `namei_ext` or FUSE. |
| Custom/stackable FS boundary | Main RQ3 boundary evidence, usually citation/source-code based. | Medium. | Use workload-specific ownership tables; avoid full-system reimplementation unless needed. |
| Materialized namespace mechanisms | Related-work/background unless a selected source oracle makes one a direct operator baseline. | Low-to-medium. | Do not reopen bind/Overlay/copy/symlink/table shootouts as the main story. |
| Filebench/Postmark/fsbench metadata workloads | RQ2 control, appendix context, or related work only. | Low. | They measure generic metadata scalability or filesystem overhead, not source-derived state-dependent path-view policy. |

## Absorbable Ideas

| Source/community | Idea to absorb | Claim expansion enabled | Experiment implication | Risk |
| --- | --- | --- | --- | --- |
| Agent filesystems | Branch, fork, checkpoint, COW, staging, whiteout, symlink, cache invalidation, final-tree oracle. | Strong Experiment A. | Fixed lifecycle matrix with lookup/readdir traces and lower-filesystem checks. | Some source effects require broader FS/runtime ownership. |
| Traditional industrial workflows | Sandboxed Application File Sharing, Build Action Sandboxing, Service Configuration and Secret Rotation, Checkpoint/Restore and Migration, HPC File Staging, and Toolchain and Dependency Environments. | Broad RQ1 evidence over one action model. | One source-derived correctness preflight per workflow; deepen the representative cases that fit the current actions. | Keep each source's broader control plane and semantics out of the claimed boundary. |
| Filesystem literature | FUSE request path, optimized FUSE context, stackable/full-FS method ownership, metadata-service responsibilities. | Stronger RQ2/RQ3. | Same-oracle FUSE plus boundary accounting; cite optimized/full-FS systems instead of multiplying weak runnable baselines. | Too many baselines can fragment the paper. |
| Recent eBPF/kernel-extension literature | eBPF safety, scheduling, virtualization, and kernel-extension placement assumptions. | Cleaner mechanism discussion. | Measure tail-latency/branch-cost where policy complexity could matter; state verifier and attachment assumptions. | This can distract unless tied to name-resolution path cost and safety boundaries. |
| Kubernetes/projected config | Service/config operational breadth. | Formal W4 `AtomicWriter` payload-view case completed; full service validation/reload remains open. | V0/V1/no-op/rollback visible objects, non-root reads, stable root and old descriptors, and unchanged lower generations. | Retrieval, materialization, symlink/inotify behavior, candidate validation, and reload orchestration remain outside the completed subset. |

## Adjacent Communities

| Community/venue family | Why relevant | Keywords/aliases | Useful papers or benchmarks |
| --- | --- | --- | --- |
| OS/filesystems | Core mechanism, baselines, safety boundary. | VFS, FUSE, stackable FS, eBPF, LSM, fanotify. | FUSE, ExtFUSE, Bento, Wrapfs, DeltaFS, IndexFS, TableFS. |
| Recent full/custom/distributed filesystems | RQ3 boundary and reviewer expectations. | multi-component FS, distributed metadata, metadata service, generated filesystem, container image FS. | Oxbow, DeLFS, FalconFS, SwitchFS, MesaFS, SpecFS/SYSSPEC, CoFS. |
| eBPF and kernel extensions | Mechanism-neighborhood safety/performance assumptions. | eBPF virtualization, BPF scheduling, BPF isolation, storage functions, kernel tunability, extension interface model. | vBPF, PeeR, KRAKENGUARD, XRP, Xkernel, bpftime/EIM, USEC. |
| AI agents/SWE agents | Main workload pressure. | agent workspace, sandbox, SWE task, terminal task. | AgentFS, BranchFS, YoloFS, Sandlock, Mirage, OpenHands, SWE-agent, SWE-ReX, Terminal-Bench. |
| Agentic systems/SRE | Broader evidence that real agentic workloads use live environments and tool orchestration. | agentic workflow, SRE agent, live benchmark, fault injection, cloud-native stack. | Murakkab, SREGym. |
| Build/environment construction | Traditional build/cache pressure. | Docker eval, fail-to-pass, environment reuse, ccache, BuildKit cache mount. | Redis/nginx/PostgreSQL build/test rows, SWE-Factory, MEnvAgent, SWE-rebench V2, DockSmith, Multi-Docker-Eval. |
| HPC software distribution | Shared-filesystem launch pressure, local relocation, and many toolchain variants. | Spindle, Pynamic, CernVM-FS variant symlink, Spack view, Lmod hierarchy. | Spindle, CernVM-FS, Spack, Nix/Guix, FxMark and mdtest for measurement. |
| Containers/orchestration | Service/config context. | projected volume, config map, secret, overlay, namespace. | Kubernetes projected volumes, OverlayFS, mount namespaces. |

## Venue Evaluation Patterns

OSDI/SOSP-grade evidence should not look like a source catalog or a pile of
microbenchmarks. RQ1 requires complete source-oracle evidence for all seven
industrial workflows. RQ2 and RQ3 use fewer, deeper same-oracle matrices. Each
matrix must pass correctness through the real KVM `cgroup/namei_ext` attach
path, compare against feature-equivalent FUSE when it answers RQ2, account for
custom/stackable filesystem ownership when it answers RQ3, preserve raw
results, and receive result review before paper interpretation. Controls and
ablations are admitted only when they change an RQ answer.

## Must-Read List

- Linux kernel docs for `sched_ext`, BPF LSM, FUSE, FUSE passthrough, OverlayFS.
- ExtFUSE, FAST 2017 FUSE study, Bento, Wrapfs.
- RFUSE, CoFS, DFUSE, FUSE-BPF, FUSE passthrough.
- Oxbow, DeLFS, FalconFS, SwitchFS, MesaFS, SpecFS/SYSSPEC.
- XRP, vBPF, PeeR, KRAKENGUARD, Xkernel, bpftime/EIM, USEC.
- AgentFS, BranchFS, YoloFS, Sandlock, Mirage.
- Murakkab and SREGym as agentic workload context only.
- SWE-Factory, MEnvAgent/MEnvData-SWE, SWE-rebench V2, DockSmith.
- DeltaFS, IndexFS, TableFS as boundary/non-goal context.
- XDG Document Portal, Bazel sandboxfs, LLNL Spindle, Nix, Guix, Spack,
  CernVM-FS variant symlinks, Lmod, Conda/venv, and systemd-sysext/confext.
- FxMark as the primary standard VFS benchmark; IOR/mdtest and Filebench as
  secondary RQ2 breadth.
- 2026-07-25 industrial demand set: DeltaBox, kubelet AtomicWriter and
  Kubernetes ConfigMap docs, ingress-nginx Lua docs, DMTCP path
  virtualization (Cluster'16, SELSE'17), ccache namespace/hard_link manual
  entries, BuildKit cache-mount reference, GitHub Actions cache docs and the
  Angular cache-poisoning writeup, nydus erofs+fscache migration
  (AWS EKS AMI #2569).

## Novelty Verdict

- BOOTSTRAP step 0005 froze the mechanism story for BUILD_AND_EVALUATE:
  ExtFUSE/FUSE-BPF remain closest mechanism pressure, feature-equivalent FUSE
  is the main RQ2 comparison, custom/stackable filesystems define the RQ3
  boundary, and table/materialized-view diagnostics remain outside the novelty
  line. The 2026-07-25 portfolio update expands source-derived RQ1 coverage
  without changing that story.
- Overall same-claim risk: medium. The surrounding systems are close and
  important, but the checked primary source families do not already claim a
  narrow BPF-controlled VFS name-resolution policy boundary that preserves
  lower-filesystem data and object semantics.
- 2026-07-23 refresh: OSDI'26, NSDI'26, FAST'26, EuroSys'26, OSDI'25, and
  OSDI'22 add strong adjacent evidence but do not force a claim shrink. They
  strengthen the mechanism ladder: materialization/native namespace tools below,
  access-control hooks around allow/deny, `namei_ext` at VFS name-resolution
  object selection, and FUSE/custom/full filesystems above when the workload
  needs filesystem-service ownership.
- Ambitious target claims: keep `namei_ext` as a `sched_ext`-style VFS
  extension point between eBPF LSM and FUSE/custom FS; keep RQ1/RQ2/RQ3 as
  expressiveness, cost versus FUSE, and ownership/responsibility boundary
  versus custom or stackable filesystems.
- Claims requiring stronger differentiation or evidence: do not claim exclusive
  necessity for `namei_ext`; do not say workloads intrinsically require eBPF;
  do not treat source characterization or prototype matrices as final RQ
  evidence.
- Larger claim opportunity: seven non-overlapping industrial workflows now map to one bounded
  action model: XDG grants, Agent workspaces, Bazel action views, service
  epochs, DMTCP restart remapping, Spindle shared-to-local relocation, and
  toolchain/profile views. Breadth comes from correctness and scope cells;
  depth comes from representative macro cases plus standard VFS benchmarks,
  not from inventing a separate baseline catalog for every source.
- Main evidence roles: feature-equivalent FUSE is the RQ2 baseline;
  source/native behavior is the RQ1 correctness oracle; custom/stackable
  ownership tables are RQ3 boundary evidence; lower-filesystem/no-hook runs are
  controls for overhead attribution.
- Experimental precedents and external assets: AgentFS/BranchFS/YoloFS/Mirage
  for workspace lifecycle and SWE-Factory-Gym for the released Click task; XDG
  portal, Bazel sandboxfs, Kubernetes
  AtomicWriter, DMTCP, Spindle, and Spack/Nix/Guix/CernVM-FS for traditional
  path views; FxMark/mdtest/Filebench for RQ2; ExtFUSE/FUSE/Bento/Wrapfs for
  comparison discipline.
- 2026-07-25 industrial demand survey
  (`docs/tmp/2026-07-25-usecase-industrial-demand-survey.md`): six domains
  re-implemented lookup-time object selection at wrong layers. Service/config
  rotation is promoted from conditional to the third use case (kubelet's
  switch already lives at name resolution; ingress-nginx re-invents
  lookup-time selection in Lua). Build/cache is repositioned as access-point
  view governance rather than acceleration. Remote filesystem cache
  (nydus FUSE→erofs+fscache) is recorded as motivation evidence only.
- Current evidence state: W1 through W7 all have reviewed formal RQ1 results;
  W2 includes a released source task, W5 includes real DMTCP restarts, and W6
  includes exact selection of 47 Spindle-created node-local objects. The Agent
  lifecycle, FxMark lookup,
  and FxMark readdir FUSE comparisons are complete. The next high-value depth
  questions are cache-cold or broader metadata RQ2 behavior and a second
  source-derived RQ3 ownership row. The existing ccache matrix remains
  supporting macro evidence rather than a headline case-study motivation.

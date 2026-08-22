# Evaluation

Last updated: 2026-08-08

This file holds the scientific evaluation state: use cases, experiment
matrices, result pointers, and open questions. Research process rules and
gates belong to the orchestrator skill, not to this repository. The previous
process-heavy version is archived at
`docs/tmp/2026-07-25-archived-process-docs/evaluation.md`.

## Research Questions

| RQ | Question | Main comparison |
| --- | --- | --- |
| RQ1 Expressiveness / sufficiency | Can a narrow VFS name-resolution extension express real state-dependent path-view policies without taking over filesystem semantics? | Source-system behavior as correctness oracle |
| RQ2 Cost / overhead versus FUSE | What is the cost of programmable policy on the VFS name-resolution path compared with a feature-equivalent FUSE policy? | Feature-equivalent FUSE over the same oracle |
| RQ3 Ownership / responsibility boundary | When the required policy is only name resolution, which filesystem methods and runtime responsibilities does `namei_ext` own compared with a custom or stackable filesystem? | Same-oracle method and responsibility accounting |

## Use Cases

### Paper-facing workload set

The paper has seven mandatory industrial RQ1 case studies. Each must reach a
complete source-oracle correctness result on the modified kernel in KVM. RQ2
performance and RQ3 ownership comparisons may use representative cases, but
they do not replace or reduce any of the seven RQ1 cases. All seven now have
complete reviewed RQ1 evidence; two also carry the deepest cross-RQ evidence:

| Role | Workload | Completed evidence | Remaining paper-value gap |
| --- | --- | --- | --- |
| Headline | W2 Agent Workspaces | RQ1 source-task and lifecycle correctness; matched FUSE RQ2; matched Wrapfs-derived RQ3 | No additional workload experiment is required |
| Second main case | W3 Build Action Sandboxing | RQ1 with two concurrent Bazel 6.5.0 actions | A valid official-sandboxfs macro comparison would add the missing traditional-application RQ2 result; the previous protocol closed without a paired run |
| Supporting breadth | W1 Sandboxed Application File Sharing | RQ1 against official `xdg-document-portal` 1.18.4 | No performance claim; the attempted official-portal timing protocol closed |
| Supporting breadth | W4 Service Configuration and Secret Rotation | RQ1 correctness plus a reviewed 20-boot source-relative scaling comparison against official Kubernetes v1.30.0 `AtomicWriter` | Quantitative evidence is limited to a preloaded policy and two known generations; service reload remains outside the result |
| Supporting breadth | W7 Toolchain and Dependency Environments | RQ1 with Ubuntu CPython 3.10/3.12 environments | No matched FUSE or custom-filesystem claim |
| Supporting breadth | W5 Checkpoint/Restore and Migration | Three fresh modified-kernel KVM boots complete DMTCP PathTranslator, `namei_ext`, and withdrawn-map conditions for the same checkpoint/restart oracle | No performance or full migration claim; DMTCP retains checkpointing, descriptor restoration, and coordination |
| Supporting breadth | W6 HPC File Staging | Three fresh modified-kernel KVM boots complete source Spindle, `namei_ext`, and withdrawn-target conditions over the same 47-object loader slice | No distribution, cache-population scalability, production launch-performance, or matched FUSE claim |

All seven rows remain mandatory and distinct; completion does not merge or
remove any case. FxMark and the historical ccache matrix remain performance
evidence outside the seven-case count. The complete data and raw-result audit is
[`docs/tmp/2026-08-02-workload-evidence-audit.md`](tmp/2026-08-02-workload-evidence-audit.md).

The evaluation separates three kinds of evidence:

1. **Case studies** replay behavior from a real source system and check its
   correctness oracle.
2. **Standard filesystem benchmarks** measure the cost of adding the hook to
   the VFS path.
3. **Mechanism-specific tests** isolate `PASS`, `HIDE`, target selection,
   policy updates, and failure behavior that standard benchmarks do not know
   about.

A case study is admitted only when an unmodified POSIX application consumes
paths, the source system changes which existing object a path denotes or
whether the object is visible, and the behavior has a reproducible oracle.
The experiment does **not** need to prove that a static table, mount, symlink,
or another mechanism is impossible. It tests whether the source behavior fits
the narrow `namei_ext` boundary. RQ2 separately compares representative
same-oracle cases with feature-equivalent FUSE; every RQ1 breadth case does not
need its own FUSE performance experiment.

The unit of classification is an industrial workflow, identified by its user,
production objective, triggering event, source system, and correctness oracle.
It is not a policy action such as `HIDE`, `SELECT`, an epoch switch, or pathname
remapping. Multiple systems that solve the same production problem are grouped
inside one case; systems that happen to use similar path actions but solve
different production problems remain separate.

### Industrial case-study portfolio

| ID | Industrial workflow | Source systems and current mechanism | State and path-view effect | Correctness oracle | `namei_ext` boundary and natural comparison |
| --- | --- | --- | --- | --- | --- |
| W1 | Sandboxed Application File Sharing | The official [Documents portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Documents.html) exposes host files to sandboxed applications through a FUSE filesystem, gives each application a restricted view, and supports grant/revoke. The portal API can derive host-application identity from a standardized [cgroup path](https://flatpak.github.io/xdg-desktop-portal/docs/api-reference.html). | `(application identity, grant state, document ID)` selects an existing host file or hides it; revocation changes subsequent lookup/readdir results. | The granted application opens/stats/enumerates the document; another application and the revoked application receive the portal's expected `ENOENT`/visibility result; bytes and inode-backed behavior match the registered host object. | Implement the existing-object grant/revoke subset and compare with the portal-style FUSE view. Synthetic document contents, mode synthesis, persistent grant storage, and portal UI stay out of scope. |
| W2 | Agent Workspaces | [AgentFS](https://github.com/tursodatabase/agentfs), [BranchFS](https://arxiv.org/abs/2602.08199), and [YoloFS](https://arxiv.org/abs/2604.13536) (SOSP 2026) provide workspace branch, snapshot, hide/whiteout, and staged/base views through FUSE or a stackable filesystem. | `(workspace, branch state, pathname)` selects a staged object, base object, or hidden result; fork, staging, and rollback change the visible workspace. | Source-derived lifecycle trace, final file-tree oracle, expected lookup/readdir/open/stat results, concurrent branch isolation, and unchanged lower-object data/metadata outside the policy decision. | Compare the same view/visibility subset with feature-equivalent FUSE. Write isolation, copy-up, commit, conflict resolution, and audit storage remain with the workspace manager or filesystem. |
| W3 | Build Action Sandboxing | Bazel's [sandboxfs](https://blog.bazel.build/2017/08/25/introducing-sandboxfs.html) was built because an action may require hundreds or thousands of mappings; symlink-forest construction was costly and error-prone, while FUSE could expose an arbitrary view. Its [official repository](https://github.com/bazelbuild/sandboxfs) is archived but reproducible. | `(action identity, declared-input set, configuration)` maps action paths to existing source or generated objects and hides undeclared paths. | A real Bazel build/test succeeds with the declared inputs, exact outputs contain the expected bytes, an undeclared-input probe fails, and two concurrent actions observe their own mappings. | Reuse an action trace and local existing targets. Compare `namei_ext` with the source symlink-forest behavior and feature-equivalent FUSE; remote execution, CAS download, output upload, and sandbox process isolation are out of scope. |
| W4 | Service Configuration and Secret Rotation | Kubernetes [AtomicWriter](https://github.com/kubernetes/kubernetes/blob/7c48c2bd72b9bf5c44d21d7338cc7bea77d0ad2a/pkg/volume/util/atomic_writer.go) materializes timestamped versions and atomically retargets `..data`; [ConfigMap documentation](https://kubernetes.io/docs/concepts/configuration/configmap/) records delayed propagation and non-updating `subPath` mounts. nginx reload is a broader application-level extension. | `(workload identity, published generation, pathname)` selects a V0 or V1 config/certificate object and hides retired names. The completed subset executes initial, update, repeated no-op, and rollback under one stable volume root. | The official `AtomicWriter` control and `namei_ext` must agree on exact bytes, modes, visible names, non-root reads, stable-root `openat()`, and old-descriptor behavior. Selected logical files must be the direct lower objects, and both lower generation trees must remain unchanged. | Reviewed correctness and a 20-boot scaling experiment support the already-materialized, two-known-generation subset. At 256 paths, the paired per-volume lifecycle ratio is 0.720 [0.680, 0.731] after policy load/attach; smaller scales favor AtomicWriter. ConfigMap retrieval, kubelet reconciliation, `..data`/symlink topology, inotify, validation, application reload, and reload failure handling remain outside this result. |
| W5 | Checkpoint/Restore and Migration | [DMTCP path virtualization](https://dmtcp.sourceforge.io/papers/cluster16.pdf) translates paths remembered before checkpoint to paths valid after migration; the implementation interposes pathname operations in user space. | `(restart image, restored root/mount state, remembered pathname)` selects the restored existing object or fails closed when the mapping is absent. | The source application checkpoints, moves to a different root, restarts, and reopens the expected object with matching bytes/metadata; an unmapped stale path fails. | Compare the same remapping with DMTCP's source plugin behavior and a withdrawn-mapping causal control. Process checkpointing, descriptor restoration, distributed coordination, and file-content transfer remain DMTCP/CRIU responsibilities; RQ2 owns representative FUSE comparisons. |
| W6 | HPC File Staging | LLNL [Spindle](https://computing.llnl.gov/projects/spindle) intercepts library, executable, Python, and selected data-file operations, distributes one shared-filesystem read to node-local storage, and redirects applications to the local object. It is [enabled automatically on El Capitan](https://hpc.llnl.gov/documentation/user-guides/using-el-capitan-systems/using-el-capitan-systems-spindle-and-library). Its public [source](https://github.com/LLNL/Spindle) interposes `open`, `stat`, `exec`, and dynamic-loader operations. | `(job/session, staged-object inventory, pathname)` selects a prepared node-local object. Withdrawing one registered target makes the covered source pathname unavailable to the unchanged loader. | Pinned Spindle in serial pull mode must populate 47 required objects and complete its loader slice; the same direct loader must complete through the selected cache objects with exact object identity and transcript; withdrawal must produce the expected loader failure without another target hit. | The completed RQ1 case reuses Spindle's cache population and first-party mapping logs, replacing only final pathname selection. `namei_ext` does not distribute bytes, populate caches, recognize libraries, or reproduce Spindle's scaling result. Source Spindle is the natural oracle; a feature-equivalent FUSE run would be a separate RQ2 extension. |
| W7 | Toolchain and Dependency Environments | [Nix profiles](https://nix.dev/manual/nix/2.34/command-ref/files/profiles.html) and [Guix profiles](https://guix.gnu.org/manual/en/guix.pdf) are versioned symlink trees into immutable stores; [Spack environment views](https://spack.readthedocs.io/en/v0.23.1/environments.html) link installed packages into `bin/lib/include` trees; Python [venv](https://docs.python.org/3/library/venv.html), [Conda](https://docs.conda.io/en/latest/user-guide/tasks/manage-environments.html), [nvm](https://github.com/nvm-sh/nvm), [rbenv](https://github.com/rbenv/rbenv), [Lmod](https://lmod.readthedocs.io/en/6.6/), and Debian [update-alternatives](https://manpages.debian.org/bookworm/dpkg/update-alternatives.1.en.html) select versions through environment, shims, profiles, or symlink groups. CernVM-FS even implements [variant symlinks](https://cvmfs.readthedocs.io/en/2.14/cpt-repo/#variant-symlinks) in FUSE so one software or certificate path resolves according to client configuration. | `(project/job/process group, selected environment, pathname)` selects one installed executable, library tree, dependency profile, or site-specific target; switching the environment changes later exec/open/readdir results without changing the application's path. | Run source-native version and build/import checks in two real environments; verify executable/library identity and output, concurrent isolation, switch, and rollback. | Reuse installed objects from Spack/Nix/Guix or two Python/Node environments. The natural source behavior remains the oracle; feature-equivalent FUSE is the RQ2 comparison. Package solving, installation, builds, activation scripts, and ABI compatibility stay with the source package manager. |

The cases remain distinct even when their policies use the same bounded action:

| Potential confusion | Non-overlap boundary |
| --- | --- |
| W2 Agent Workspaces vs W5 Checkpoint/Restore and Migration | W2 creates concurrent, isolated workspace branches for agents; W5 resumes one traditional application after restart or migration and preserves its pathname continuity. |
| W3 Build Action Sandboxing vs W7 Toolchain and Dependency Environments | W3 restricts each action to declared build inputs and tests undeclared-input failure; W7 selects an installed compiler/runtime/dependency environment and tests version, linkage, import, and program output. |
| W6 HPC File Staging vs W7 Toolchain and Dependency Environments | W6 moves the same object from shared storage to node-local storage to reduce launch and shared-filesystem load; W7 chooses among semantically different installed software variants. |
| W4 Service Configuration and Secret Rotation vs W7 Toolchain and Dependency Environments | W4 publishes validated configuration, certificate, or secret releases to a running service; W7 selects a user, project, or job software environment. |

W1--W7 are the seven promised industrial RQ1 case studies, not seven unrelated
performance benchmarks. Multiple source systems are consolidated within the
workflow they serve, but no workflow is removed or merged into another.
Every case requires a complete source-oracle correctness result for RQ1. RQ2
uses standard benchmarks plus feature-equivalent FUSE on representative macro
cases, and RQ3 uses a matched custom or stackable implementation where it
changes the ownership conclusion; neither RQ2 nor RQ3 requires seven bespoke
comparisons.

### Related evidence, not case studies

| Candidate | Citation | Disposition |
| --- | --- | --- |
| SELinux polyinstantiation / `pam_namespace` | [`namespace.conf(5)`](https://man7.org/linux/man-pages/man5/namespace.conf.5.html); Red Hat SELinux guide | Closest shipping precedent for per-context path views. Cite and account for it in RQ3; no separate workload is needed. |
| Plan 9 per-process namespaces and union directories | Pike et al., *The Use of Name Spaces in Plan 9*, 1993 | Intellectual ancestor and related work, not a Linux evaluation workload. |
| lakeFS and DVC | Git-like branches over object storage | Demand is real, but normal consumption is through S3 APIs/SDKs rather than VFS pathname lookup; related work only. |

The paper claims that these source behaviors can share one bounded
name-resolution decision boundary. It does not claim that the workloads
intrinsically require eBPF or that their existing mechanisms cannot implement
the behavior.

## Performance Evaluation

### Standard benchmarks

| Benchmark | Role and operations | Conditions | Primary metrics and figure |
| --- | --- | --- | --- |
| [FxMark](https://github.com/sslab-gatech/fxmark) ([ATC'16 paper](https://www.usenix.org/conference/atc16/technical-sessions/presentation/min)) | Primary VFS microbenchmark: component/path lookup (`MRPL`, `MRPM`, `MRPH`), directory enumeration (`MRDL`, `MRDM`), and create control (`MWCL`). | Stock kernel; patched kernel unattached; attached `PASS`; attached same-filesystem `SELECT`; feature-equivalent FUSE. Run cache-hot and cache-cold variants where supported and scale threads/vCPUs. | Operations/s and normalized overhead versus stock across core count; median and confidence interval across independent runs. Main figure: throughput curves or a compact operation-by-condition heatmap. |
| [mdtest/IOR](https://github.com/hpc/ior) | Standard metadata breadth: file/directory create, stat, open/read, rename, and remove. It checks that the conclusion is not unique to FxMark's operation mix. | Stock, patched-unattached, attached `PASS`, and FUSE; selected operations only, with fixed tree and process counts. | Metadata operations/s and scaling efficiency. Appendix or secondary figure. |
| [Filebench](https://github.com/filebench/filebench) | Mixed macro control using fileserver and webserver profiles. It is not evidence for RQ1. | Stock, patched-unattached, attached `PASS`, representative `SELECT`, and FUSE with identical lower storage and cache warmup. | Runtime/throughput, CPU utilization, context switches, and FUSE daemon CPU. Secondary macro figure. |

### Mechanism-specific measurements

The repository's custom VFS benchmark remains necessary for operations that
standard suites do not expose:

| Measurement | Conditions | Metrics |
| --- | --- | --- |
| Hook fast path | Stock kernel versus patched-unattached, then attached `PASS`, for `stat`, `open`, `access`, `exec`, multi-component walk, and `readdir` | ns/op, p50/p95/p99, cycles, instructions, branch misses |
| Policy actions | `PASS`, `HIDE`, same-filesystem `SELECT`, cross-filesystem `SELECT`, and invalid/fail-closed decision | ns/op and tail latency per operation; returned errno/object identity |
| State update | Policy/object registration update followed by concurrent lookup | update calls/s, update-to-visible latency, stale observations after acknowledged update |
| FUSE comparison | Same policy inputs, targets, operation stream, cache state, daemon count, and justified caching/passthrough mode | client latency/throughput, daemon CPU, context switches, request count, correctness oracle |

The critical kernel-maintainer result is the patched-but-unattached cost.
Macro compile time cannot establish that fast path because compiler work
dilutes pathname lookup overhead.

### VFS construction validation

These checks validate the interface construction separately from workload
breadth and performance:

| Invariant | Evidence | Current result |
| --- | --- | --- |
| RCU/ref-walk selection | Force final-directory and intermediate-component selection with `openat2(RESOLVE_CACHED)` | Passed in modified-kernel KVM; both entered selected-target handling from RCU-walk and completed after in-place legitimization without a full path-walk restart |
| Registered-target replacement | Replace one target ID 128 times while another process continuously reads through the selected pathname | Passed; every read observed one complete old or new payload |
| Exact-parent prefilter | Attach policy to one parent, probe a second parent, and run an attached exact-empty counter control | Functional lookup/readdir kept the unrelated lower view; the control recorded zero BPF invocations |
| Selected-object semantic preservation | Pair direct lower paths with selected paths for a frozen 16-case, 80-operation matrix on ext4/tmpfs; attribute every selected operation to target, PASS, or no policy; retain an opened selected directory across policy teardown | Three fresh KVM boots passed 48/48 direct and 48/48 selected cases, 240/240 per-operation engagements, six raw identity checks, and 96/96 residual checks. Post-teardown descriptor-relative create/write/read/rename/unlink completed with zero policy engagement |
| Policy execution semantics | Convert in place where possible and permit a full VFS `-ECHILD` restart | The wrapper does not reinvoke policy merely to apply a saved action, but a full restart is at-least-once; policy side effects must be idempotent |

The first three construction rows are recorded in
`docs/tmp/2026-07-27-namei-ext-rcu-target-selection-implementation.md`,
`docs/tmp/2026-07-27-namei-ext-global-parent-fast-path-implementation.md`, and
the corresponding Phase 1 result roots. The semantic-preservation row is
recorded in
`docs/tmp/2026-08-01-semantic-continuation-formal-v2-result.md` and its immutable
formal-v2 result root. The restart row is an ABI semantic, not an exactly-once
claim.

The attempted RQ3 target-lifetime sanitizer matrix is closed without a formal
result. Its final preflight passed the normal and KASAN mechanism analyses and
all KCSAN runner oracles, but strict KCSAN recorded 99 report blocks during the
workload windows. The frozen diagnostic rule therefore made the complete
preflight negative, and the planned nine-boot matrix did not run. This result
does not replace the bounded 128-replacement construction check above and is
not paper evidence. The immutable outcome and stopping decision are recorded
in `docs/tmp/2026-07-31-rq3-target-lifetime-preflight10-kcsan-negative.md`.

The attempted official-source W1 RQ2 comparison is closed without a performance
result. Its third and final preflight exercised both official
`xdg-document-portal` 1.22.1 and `namei_ext` end to end on dedicated ext4
fixtures. Both mechanisms passed the same 100 measured application-visible
transactions and showed positive file and directory mechanism engagement, but
the portal daemon created a tenth thread during the measured interval. The
frozen analyzer therefore could not compute the required all-thread resource
delta. The run is invalid for performance, no formal ten-pair execution is
authorized, and no latency from the preflight is paper evidence. The complete
judgment is
`docs/tmp/2026-08-01-rq2-official-xdg-portal-preflight-review.md`.

## Experiment Matrix Status

### Case-study implementation status

| Case | Source/oracle fixed | `namei_ext` correctness in KVM | Feature-equivalent FUSE | RQ3 boundary record |
| --- | --- | --- | --- | --- |
| W1 Sandboxed Application File Sharing | Official `xdg-document-portal` 1.18.4 and its FUSE view execute the existing-object, two-application grant/revoke oracle before the matched `namei_ext` arm | Passed and independently reviewed: three fresh KVM boots, 15/15 official-source states, 15/15 `namei_ext` states, exact state-by-state operation agreement, and exact visible logical/lower identity for `namei_ext` | The official source FUSE implementation executed as the RQ1 correctness control; no matched performance result is claimed | Both mechanisms preserved their direct lower object; the unrelated same-named path, source teardown, policy/target/cgroup cleanup, midpoint isolation, and dmesg checks passed in every boot |
| W2 Agent Workspaces | AgentFS-derived lifecycle plus released SWE-Factory-Gym `pallets__click-2622` source task fixed | Passed and independently reviewed: the lifecycle matrix plus three fresh source-task boots, 12/12 policy-backed task states, 6/6 physical source controls, concurrent completed/base views, switch, rollback, and withdrawal | Same-oracle formal comparison passed: 10 paired blocks, 20 KVM boots, 10,000 lifecycle samples per condition | Formal matched `namei_ext`/Wrapfs-derived experiment passed: 37/37 pairwise oracles for both mechanisms, 21/21 fault cells, and runtime attribution in each of three KVM boots |
| W3 Build Action Sandboxing | Bazel 6.5.0 two-genrule oracle fixed: same logical path, distinct declared roots, undeclared-input lookup/readdir probe, concurrent overlap | Passed and independently reviewed: three fresh KVM boots, six Bazel actions, six action-specific logical/lower inode matches, twelve preserved lower objects, and all allow/hide/select branches | Not run; RQ2 owns the separately frozen sandboxfs comparison | Policy/target/cgroup cleanup and lower-object preservation passed in every boot; full ownership table open |
| W4 Service Configuration and Secret Rotation | Official Kubernetes v1.30.0 `AtomicWriter` payload publication fixed as V0, V1, V1 no-op, and V0 rollback under one stable root; full nginx live reload remains a separate extension | Correctness passed in the original three-boot case and in a reviewed 20-boot quantitative matrix: 800/800 lifecycles, 170,000 identity observations, and 67,200 lower-preservation observations passed | Not run; the quantitative result compares source-system materialization rather than FUSE | Supporting RQ1 evidence. At 256 paths, the preloaded-policy per-volume lifecycle ratio is 0.720 [0.680, 0.731]; smaller scales favor AtomicWriter. No cold-start, fewer-object, running-kubelet, service-reload, or broader filesystem claim |
| W5 Checkpoint/Restore and Migration | DMTCP commit `068559d9b14c` is built from Git with the disclosed restart-environment scan fix; the official `pathvirt` test and source-derived A-to-B restart oracle pass from relocated result-owned installs | Passed and independently reviewed: three fresh KVM boots, nine real DMTCP checkpoint/restart conditions, PathTranslator and `namei_ext` A-to-B transitions through one pathname, and three withdrawn-map `ENOENT` controls. Restart-time `SELECT` rose 12-to-24 only with the installed mapping and remained 12-to-12 after withdrawal | Not required for this RQ1 case; RQ2 owns matched FUSE comparisons | All nine checkpoint images, 18 application observations, 165 controller observations, 108 before/after lower-object rows, and BPF/cgroup cleanup passed. DMTCP retains checkpointing, descriptor restoration, and coordination; no performance or broad migration claim is made |
| W6 HPC File Staging | Pinned LLNL Spindle commit `8853636` in serial pull mode populates 47 exact cache objects and supplies their first-party source-to-local mappings | Passed and independently reviewed: three fresh KVM boots, 3/3 source Spindle, 3/3 `namei_ext`, and 3/3 withdrawn conditions; 141/141 mappings, selections, identities, and preservation rows; 204 attributed `SELECT` hits; 3/3 lower-permission and withdrawn-target controls | Not required for this RQ1 case; a matched FUSE run would be separate RQ2 work | Spindle retains object recognition, distribution, and cache population. Policy/target/cgroup/mount/process cleanup and dmesg gates passed in every boot; no performance or production-scaling claim is made |
| W7 Toolchain and Dependency Environments | Ubuntu CPython 3.10/3.12 `venv` workflow and interpreter/package oracle fixed | Passed and independently reviewed: three fresh KVM boots selected two existing environments through the same logical executable path, including paired start, switch, and rollback | Not required for this RQ1 sufficiency row; RQ2 owns matched FUSE comparisons | Logical root/interpreter identity matched the selected lower objects; controls observed lower `EACCES` and withdrawn `ENOENT`; inventoried type/mode/owner/size/device/inode/mtime fields were unchanged |

### A. Sandboxed Application File Sharing (supporting RQ1 breadth)

| Cell | Status | Raw root |
| --- | --- | --- |
| Official-source two-application grant/revoke lifecycle | Passed and independently reviewed in three fresh modified-kernel KVM boots. Official `xdg-document-portal` 1.18.4 and `namei_ext` each passed 15/15 states with exact state-by-state agreement: application A was hidden before grant, visible immediately after grant, and hidden immediately after revoke; application B remained hidden | `results/experiments/application-file-sharing-source-oracle-rq1/20260730T-xdg-source-formal01/` |
| Lookup, read, enumeration, and object identity | Passed: all 24 hidden states across both mechanisms returned `ENOENT` for document/payload operations and completed readdir without the document; all six granted states listed the name and read the complete expected payload. The three granted `namei_ext` states matched logical/lower document and payload device/inode | Same raw root |
| Lower object, unrelated path, and cleanup | Passed: both mechanisms preserved lower-object device, inode, mode, owner, size, timestamps, and bytes in all three boots; unrelated-object controls passed; source teardown, midpoint isolation, policy detach, target clear, six cgroup removals, three-stage external BPF/FUSE inventories, and dmesg scans passed | Same raw root |
| Policy and source engagement | All five pinned upstream portal tests executed without skip/failure/timeout. The official portal FUSE view mounted and served the source states. `namei_ext` recorded 210 lookup, 30 readdir, 3 `SELECT`, 12 lookup `HIDE`, and 4 readdir `HIDE` events per boot | Same raw root |
| Independent result review | Valid supporting RQ1 source-fidelity evidence. The claim is the official portal's existing-object read grant/isolation/revoke subset, not complete portal compatibility, sandbox enforcement, writes, persistence, or performance | `docs/tmp/2026-07-30-application-file-sharing-source-oracle-rq1-formal01-result-review.md` |
| Matched official FUSE performance comparison | Closed without a result after three permitted preflights. Attempt 3 passed 100 official-portal and 100 `namei_ext` policy-view transactions plus direct controls, but a changing portal thread set invalidated the mandatory resource analysis. No latency or ratio is admitted and no formal run is authorized | `results/experiments/application-file-sharing-rq2-official-preflight/20260801T113049Z-3ee783a/`; `docs/tmp/2026-08-01-rq2-official-xdg-portal-preflight-review.md` |

Entry points:
`make kvm-application-file-sharing-source-oracle-preflight` and
`make experiment-application-file-sharing-source-oracle-rq1`.

### B. Agent Workspaces (headline)

| Cell | Status | Raw root |
| --- | --- | --- |
| RQ1 correctness: AgentFS-derived trace oracle for `namei_ext` and feature-equivalent FUSE | Passed, independently reviewed; 3 terminal KVM runs, 1,170 pass-bearing records with zero failed oracles plus 6 metadata records in each run, clean dmesg | `results/experiments/agent-workspace-matrix/20260722T020120Z-rq1run1/`, `20260722T020210Z-rq1run2/`, `20260722T020245Z-rq1run3/` |
| RQ1 released source task | Passed and independently reviewed in three fresh modified-kernel KVM boots: 12/12 policy-backed task states and 6/6 physical source controls passed. Two concurrent process groups used one logical pathname but selected distinct completed/base Click roots; the released task observed 40/40 versus 39/40 tests. Fresh-child switch and rollback changed both object identity and task outcome, while withdrawal hid the workspace. All import-path, lower-preservation, cleanup, inventory, and dmesg gates passed | `results/experiments/agent-workspace-source-task-rq1/20260729T-agent-source-task-formal01/`; `docs/tmp/2026-07-29-agent-workspace-source-task-rq1-formal01-result-review.md` |
| RQ2 controlled lifecycle timing versus FUSE | Passed and independently reviewed: lifecycle p50 5.51 us versus 62.64 us, paired FUSE/namei_ext ratio 11.32x [11.24, 11.64]; 20/20 boots, 20,000/20,000 lifecycle samples, and 960/960 required oracles passed | `results/experiments/agent-workspace-rq2/20260727T-agent-workspace-rq2-formal-v3/` |
| RQ2 operation decomposition | Mixed by design: `open` 8.35x and `readdir` 13.59x in favor of namei_ext; cache-hit `stat` and `access` favored FUSE; `exec` was inconclusive. This is a scoped AgentFS-derived lifecycle result, not an end-to-end agent-task speedup or a generic FUSE claim | Same raw root and `analysis/report.md` |
| RQ2 result review | Valid; predeclared hypothesis supported; admitted as an OSDI/EuroSys-quality controlled mechanism result with supporting paper value | `docs/tmp/2026-07-27-agent-workspace-rq2-formal-v3-result-review.md` |
| RQ3 matched stackable-FS boundary | Passed and independently reviewed: both `namei_ext` and the Wrapfs-derived implementation passed 37/37 pairwise AgentFS-derived oracles in all three boots; two verifier faults had exact rejection logs and all 19 runtime fault cells preserved lower-object manifests; 13 Wrapfs operation classes were observed at runtime | `results/experiments/agent-workspace-rq3-formal/20260728-rq3-formal-v3/` |
| RQ3 result review | Valid for the existing-object Agent workspace slice; supports a narrower executed method and runtime-responsibility boundary, not a general comparison of complete filesystems | `docs/tmp/2026-07-28-agent-workspace-rq3-formal-v3-result-review.md` |

### C. Build Action Sandboxing (traditional main case)

| Cell | Status | Raw root |
| --- | --- | --- |
| Current declared-input allowlist with two concurrent Bazel 6.5.0 genrules | Passed and independently reviewed in three fresh modified-kernel KVM boots; all six actions reached the paired barrier and completed | `results/experiments/build-action-sandboxing-rq1/20260729T180121Z-w3-formal02/` |
| Same logical path, distinct registered lower roots | Passed: all six logical/lower device and inode pairs matched, while action A and B produced their distinct expected 17-byte outputs | Same raw root |
| Undeclared existing input hidden from lookup and readdir | Passed: every boot recorded 8 `SELECT`, 6 allow-lookup, 2 allow-readdir, 4 hide-lookup, and 6 hide-readdir decisions | Same raw root |
| Lower-filesystem preservation and cleanup | Passed: all twelve declared/undeclared lower-object records preserved device, inode, mode, size, and expected bytes; every boot recorded policy detach, two target clears, two cgroup removals, empty post-run BPF/FUSE inventory, and clean dmesg | Same raw root |
| Independent result review | Valid; tested hypothesis supported with supporting research value, scoped to the tested Bazel existing-object action-view subset | `docs/tmp/2026-07-29-build-action-sandboxing-rq1-formal02-result-review.md` |
| Matched official sandboxfs 0.2.0 comparison | No performance result. The frozen RQ2 implementation exhausted three failed paired preflights before reaching the sandboxfs arm. Attempt 3 proved that both real Bazel actions ran successfully and exposed a deterministic harness-oracle mismatch: marker files included a newline while the runner required an exact no-newline value. The mismatch is repaired, but the closed protocol does not authorize a fourth run; a separately reviewed dependency plan is required before another paired preflight | `docs/tmp/2026-07-29-build-action-rq2-experiment-plan.md`; `docs/tmp/2026-07-28-build-action-rq2-kvm-preflight-attempt-3.md` |

Correctness entrypoints: `make kvm-build-action-sandboxing-preflight` and
`make experiment-build-action-sandboxing-rq1`.
RQ2 dependency entrypoint:
`make kvm-build-action-rq2-preflight RUN_ID=<fresh-id>`.
The formal `make experiment-build-action-rq2` path is implemented but remains
outside aggregate formal-suite membership. The three-attempt preflight
protocol is closed without a valid pair, so no formal run is authorized.

### D. Kubernetes ConfigMap Publication (supporting RQ1 breadth)

| Cell | Status | Raw root |
| --- | --- | --- |
| Official source control | Kubernetes v1.30.0 `AtomicWriter` at commit `7c48c2bd...`; selected upstream tests passed, and 12/12 source states passed across three fresh KVM boots | `results/experiments/kubernetes-configmap-publication-rq1/20260729T-kubernetes-configmap-publication-rq1-01/` |
| Stable payload-view transition | Passed: V0 initial, V1 update, V1 no-op, and V0 rollback selected or hid four leaf paths under one stable root; 12/12 logical states and 6/6 direct controls passed exact bytes, mode, membership, and object-identity checks | Same raw root |
| Descriptor and permission behavior | Passed: 24/24 stable-root dirfd checks and 12/12 old-fd checks; all 30 non-root consumers read both the selected mode-0600/0644 config and mode-0400 certificate; all 90 present file observations had the recorded owner | Same raw root |
| Lower-FS preservation and cleanup | Passed: 36/36 lower objects retained bytes and metadata; all 72 lifecycle cases passed; no configured project dmesg failure signature or residual BPF/FUSE state was observed | Same raw root |
| Independent result review | `GO` for the leaf-level, already-materialized payload-view subset only; no ConfigMap retrieval/materialization, symlink/inotify, application reload, performance, or FUSE claim | `docs/tmp/2026-07-29-kubernetes-configmap-publication-rq1-result.md` |
| Quantitative matrix and correctness | Valid and independently reviewed: 20 fresh KVM boots, 800/800 lifecycle rows, 170,000 identity rows, 67,200 lower-preservation rows, 400 AtomicWriter materialization audits, and 400 unmanaged-view checks passed | `results/experiments/kubernetes-configmap-quantitative/20260808T142207Z-w4-quantitative-formal01/` |
| Per-volume lifecycle scaling | With policy load/attach excluded by the predeclared preloaded-policy boundary and reported separately, `namei_ext / AtomicWriter` is 1.742 [1.725, 1.771], 1.614 [1.565, 1.651], 1.237 [1.220, 1.269], and 0.720 [0.680, 0.731] at 4, 16, 64, and 256 paths | Same raw root and `analysis/summary.json` |
| 256-path decomposition | Median setup is 17.141 ms versus 0.181 ms, publication is 0.004 ms versus 25.517 ms, and consumer work is 6.325 ms versus 6.615 ms for `namei_ext` and AtomicWriter. The measured crossover coincides with `namei_ext` setup rising from 10.428 ms at 4 paths to 17.141 ms at 256 paths, while AtomicWriter publication rises from 2.563 ms to 25.517 ms; this decomposition is explanatory rather than a causal ablation | Same raw root and `analysis/decomposition.csv` |
| Quantitative boundary and review | Valid supporting RQ1 evidence for a preloaded policy and two known generations. It is not cold-start or unqualified end-to-end performance; repeating the separately measured attach for every volume removes the 256-path advantage. The run does not support fewer regular objects | `docs/tmp/2026-08-08-kubernetes-configmap-quantitative-formal01-result-review.md` |

Correctness entry points: `make kvm-kubernetes-configmap-publication-rq1-preflight`
and `make experiment-kubernetes-configmap-publication-rq1`. Quantitative entry
points: `make kvm-kubernetes-configmap-quantitative-preflight` and
`make experiment-kubernetes-configmap-quantitative`.

### E. Checkpoint/Restore and Migration (supporting RQ1 breadth)

| Cell | Status | Raw root |
| --- | --- | --- |
| Source and application oracle | DMTCP commit `068559d9b14c` with the disclosed one-line restart-environment scan-bound fix; all three fresh KVM boots ran PathTranslator, `namei_ext`, and withdrawn-map conditions through real checkpoint images and restarts | `results/experiments/checkpoint-restore-rq1/20260802T111000Z-w5-formal01/` |
| Same-path A-to-B restart | Passed in all six positive conditions: PathTranslator and `namei_ext` both changed the application's unchanged pathname from generation A before checkpoint to generation B after restart, with logical inode identity matching the selected physical object | Same raw root |
| Causal attribution | Passed in every boot: installed-policy `SELECT` increased from 12 before checkpoint to 24 after restart; after withdrawing the rule it remained 12-to-12 and the restarted application received the expected `ENOENT` | Same raw root |
| Checkpoint and lower-object evidence | Nine real DMTCP checkpoint images, 18 application observations, and 165 controller observations passed. All 54 before and 54 after lower-object rows matched device, inode, mode, size, mtime, and contents | Same raw root |
| Cleanup and independent review | BPF program and cgroup inventories were empty before and after every boot. The result is valid W5 RQ1 evidence for moved-root reopen of existing objects; it is not a performance, open-descriptor redirection, multiprocess, cross-host migration, or general DMTCP/CRIU claim | `docs/tmp/2026-08-02-w5-checkpoint-restore-formal01-result-review.md` |

Entry points: `make kvm-checkpoint-restore-preflight` and
`make kvm-checkpoint-restore-rq1`.

### F. HPC File Staging (supporting RQ1 breadth)

| Cell | Status | Raw root |
| --- | --- | --- |
| Source Spindle and cache population | Passed in three fresh modified-kernel KVM boots. Pinned LLNL Spindle commit `8853636` ran in serial pull mode, exited zero with empty launcher stderr, and produced exactly 47 first-party source-to-node-local mapping records per boot | `results/experiments/spindle-staging/20260802T114220Z-w6-formal01/` |
| Same loader through `namei_ext` | Passed 3/3: the unchanged upstream loader exited zero with its exact 44-line progress transcript. All 141 registered-object selection rows passed, with 204 aggregate `SELECT` hits and at least one hit for each target | Same raw root |
| Selected-object identity and lower semantics | Passed 141/141 logical-path identity probes against the exact Spindle-created tmpfs objects. All 141 preservation rows retained before/after source and cache metadata and ended with byte-equal source/cache objects; all three mode-`000` target probes returned lower-filesystem `EACCES` | Same raw root |
| Withdrawn-target control | Passed 3/3: removing the `libtest10.so` target left its hit count unchanged at five and made the same loader exit 255 with the expected diagnostic | Same raw root |
| Cleanup and scope | Guest prepare, workload, cleanup, BPF/FUSE inventory, and dmesg gates passed in every boot. Valid only for Spindle-populated final-object selection; no distribution, cache-population scalability, production launch-performance, or FUSE comparison claim | `docs/tmp/2026-08-02-w6-spindle-formal01-result-review.md` |

Entry points: `make kvm-spindle-staging-preflight` and
`make experiment-spindle-staging`.

### G. Toolchain and Dependency Environments (supporting RQ1 breadth)

| Cell | Status | Raw root |
| --- | --- | --- |
| CPython environment selection | Passed in three fresh modified-kernel KVM boots: all 18 physical/logical state records and all 24 independent Python probes passed for Ubuntu CPython 3.10.19 and 3.12.3 | `results/experiments/toolchain-environment/20260729T171551Z-toolchain-formal01/` |
| Paired process-group views and transition | Passed: both cgroups were released from the same barrier with distinct 3.10/3.12 views in every boot; application A then observed 3.12 after switch and 3.10 after rollback through the unchanged logical path | Same raw root |
| Source-system oracle and mechanism engagement | Passed: expected `sys.executable`, `sys.prefix`, SOABI, pip environment, `pyvenv.cfg`, imports, and all 18 `pip check` runs; each boot recorded 159,162 lookup events, 6,739 `SELECT` events, and positive hits for all three registered targets | Same raw root |
| Lower-FS controls and cleanup | Passed: controls observed `EACCES` after mode 000 and `ENOENT` after mapping withdrawal; selected logical root/interpreter inodes matched the corresponding lower objects; each boot's 3,270-row type/mode/UID/GID/size/device/inode/mtime inventory was unchanged; BPF/FUSE inventory was empty after teardown and dmesg passed the declared failure scan | Same raw root |
| Independent result review | Valid as supporting RQ1 evidence only; it does not support performance, FUSE/custom-FS superiority, all toolchain managers, or preservation of unobserved filesystem semantics | `docs/tmp/2026-07-29-toolchain-environment-formal-result-review.md` |

Entry points: `make kvm-toolchain-environment-preflight` and
`make experiment-toolchain-environment`.

### H. ccache compile macrobenchmark (existing performance evidence)

| Cell | Status | Raw root |
| --- | --- | --- |
| Verified hot-cache compile, 20 samples, `namei_ext` / native / FUSE | Passed in KVM; 400/400 output-oracle checks per mechanism; observed FUSE/namei_ext compile-time ratio 2.18x, native/namei_ext 0.945x | `results/experiments/build-cache/20260723T-build-cache-release-v1/` |
| Trace-derived state row (verified-hit→local, epoch→canonical) | Passed in KVM for `namei_ext` and FUSE | `results/experiments/build-cache/20260723T-build-cache-state-release-v1/` |
| Real compiler-output epoch switch, 20 samples, 2 epochs | Passed in KVM; 800/800 output matches; observed FUSE/namei_ext ratio 2.10x; policy session updates 20 vs 800 backing invalidations | `results/phase1/20260724T-epoch-switch-release-v2/` |
| Stale-local and corrupt-hidden fallback | One-sample probes passed in KVM for both mechanisms | `results/phase1/20260724T-bad-local-stale-smoke-v1/`, `-corrupt-hidden-smoke-v1/` |
| Real-compile miss cell | Open | — |
| Release-scale stale/corrupt compile cells | Open (probes only) | — |
| Timing uncertainty (median/dispersion across samples) | Open: ratios are release-run observations, not modeled statistics | — |
| RQ3 boundary table | Open | — |

Current case-study entrypoints: `make experiments`,
`make kvm-agent-workspace-matrix`,
`make experiment-application-file-sharing-rq1`,
`make experiment-build-action-sandboxing-rq1`,
`make experiment-toolchain-environment`,
and `make experiment-kubernetes-configmap-publication-rq1`.
The completed checkpoint/restore entrypoint is
`make kvm-checkpoint-restore-rq1`.
The completed HPC file-staging entrypoint is
`make experiment-spindle-staging`.

The W4 AtomicWriter payload-view subset and its source-relative quantitative
extension are complete. The quantitative claim is limited to a preloaded
policy, two known generations, and the measured per-volume lifecycle. The
older nginx live-reload V2 dependency protocol remains closed after three failed preflights;
`make experiment-service-config-rotation` is not paper evidence and requires a
separately reviewed dependency plan before reuse.

Historical ccache reproduction entrypoints: `make legacy-build-cache`,
`make kvm-w4-ccache-bulk-compile-epoch-switch`,
`make kvm-w4-ccache-bulk-bad-local-fallback`.

The ccache matrix is useful RQ2 macro evidence and validates state changes in
the prototype. It is not, by itself, a headline case-study motivation:
ccache already implements cache lookup and validation in user space. W3 Bazel
action views and W6 Spindle file relocation provide the source-derived
build/cache cases whose pathname view is the behavior under study.

### I. FxMark path-resolution cost (decisive RQ2 mechanism result)

| Cell | Status | Raw root |
| --- | --- | --- |
| Clean formal matrix | Valid and publication-usable: 50 fresh KVM boots, 450/450 unique passing observations, clean source/kernel provenance, matched stock/patched build identities, stable TSC, and no declared dmesg failure | `results/experiments/fxmark-rq2/20260728T-rq2-rcu-target-formal-v3/` |
| Broad patched-unattached versus stock matrix | Mixed under the predeclared gate: all medians are `0.981--1.013`, but MRPL 2/4-worker CI lower bounds are `0.966` and `0.958`, below the required `0.97`; this broad matrix motivated the isolated confirmatory run rather than supporting an unused-fast-path claim by itself | Same raw root and `analysis/report.md` |
| Attached `SELECT` versus optimized FUSE | Supported in all nine cells: median throughput ratio `1.052--1.088`, every paired 95% CI above `1`; SELECT wins 89/90 individual pairs | Same raw root and `analysis/summary.json` |
| Active-path decomposition | `PASS`/unattached medians are `0.901--0.934`; `SELECT`/`PASS` is `0.981--0.999`; the complete `SELECT` path retains `0.895--0.931` of unattached throughput | Same raw root; independently recomputed from raw JSONL |
| Strong FUSE engagement | The multithreaded libfuse 2.9.9 baseline enables kernel and metadata caching; measured-phase requests are `0--19` per cell, so the result does not depend on one daemon round trip per lookup | Same raw root |
| Host-pinned unused-fast-path confirmation | Supported in all three MRPL cells across 30 paired blocks and 60 fresh KVM boots: unattached/stock is `1.0009 [0.9921, 1.0036]`, `1.0083 [0.9950, 1.0179]`, and `1.0007 [0.9918, 1.0139]` at 1/2/4 workers | `results/experiments/fxmark-fast-path/20260728T-fxmark-fast-path-formal-v1/` |
| Corrected directory-enumeration breadth | Valid and publication-usable: 50 fresh KVM boots and 300/300 condition-run observations across six operation/worker cells passed. `SELECT/FUSE` was `2.20--3.66x` with every paired 95% CI above one in all three private-directory cells and the one/two-worker shared-directory cells. Shared-directory four-worker throughput was indistinguishable: `1.018x [0.907, 1.135]`. The frozen overall verdict is `mixed`, and the contention-bound cell remains in the paper | `results/experiments/fxmark-readdir/20260729T082800Z-fxmark-readdir-formal-v1/`; `docs/tmp/2026-07-29-rq2-fxmark-readdir-formal-v1-result-review.md` |
| Current result reviews | Both runs are valid. Formal-v3 closes the active SELECT/FUSE comparison and quantifies active-policy cost; the isolated confirmation closes the predeclared unused-fast-path gate for cache-hot MRPL on this host | `docs/tmp/2026-07-28-rq2-fxmark-formal-v3-result-review.md`; `docs/tmp/2026-07-28-fxmark-fast-path-formal-v1-result-review.md` |
| Prior provenance-defective matrix | Historical numerical diagnosis only: 50 boots and 450 observations, but patched binary was `g83d52c2168e2-dirty` rather than the recorded clean commit | `results/experiments/fxmark-rq2/20260727T-rq2-rcu-target-full-v2/` |
| Prior invalid-run review | Required the clean committed-kernel reproduction now completed above | `docs/tmp/2026-07-27-rq2-fxmark-rcu-target-rerun-review.md` |
| Interrupted full attempt | External five-hour timeout after 33/50 complete boots; preserved as raw evidence and excluded from every result | `results/experiments/fxmark-rq2/20260727T-rq2-rcu-target-full-v1/` |
| Superseded pre-redesign matrix | Historical valid matrix on the earlier mechanism: 50 boots and 450 condition-run observations, retained as the diagnosis that motivated the mechanism repair; not a current paper performance result | `results/experiments/fxmark-rq2/20260726T-rq2-fxmark-full-v2/` |
| Superseded result review | Earlier mechanism contradicted the hypothesis; the review required redesign and the unchanged fresh matrix that is now complete above | `docs/tmp/2026-07-27-rq2-fxmark-result-review.md` |
| Exact-parent invocation attribution | Directional KVM preflight passed; policy runs fell from about 10 to about 1 per work unit, with about 25 ns in the BPF body per run | `results/experiments/fxmark-rq2-preflight/20260727T-policy-parent-run-count-v1/` |
| Exact-parent normal preflight | Directional only: stock 2.471M, unattached 2.332M, `PASS` 1.235M, `SELECT` 1.085M, FUSE 2.041M ops/s; dispatch-count repair did not justify a new full matrix | `results/experiments/fxmark-rq2-preflight/20260727T-policy-parent-preflight-v1/` |
| Exact-empty invocation check | Passed in KVM: an attached PASS program remained stable and executed zero times during the measured `empty` cell | `results/experiments/fxmark-rq2-preflight/20260727T-exact-empty-run-count-v2/` |
| Exact-empty normal preflight | Directional only: unattached 2.424M, `empty` 1.251M, `PASS` 1.222M, `SELECT` 1.109M, FUSE 2.000M ops/s; zero-invocation `empty` reached only 0.516x unattached throughput | `results/experiments/fxmark-rq2-preflight/20260727T-exact-empty-preflight-v1/` |
| Exact-empty result review | Full matrix remains blocked; repeated pre-BPF dispatch and scope work is the next mechanism target | `docs/tmp/2026-07-27-fxmark-exact-empty-dispatch-diagnostic.md` |
| Global parent-filter preflight | Committed directional run: stock 2.463M, unattached 2.356M, `empty` 2.403M, `PASS` 2.166M, `SELECT` 1.698M, FUSE 1.938M ops/s; `empty` recovered to 1.020x unattached and PASS reached 1.118x FUSE | `results/experiments/fxmark-rq2-preflight/20260727T-parent-fast-path-8fd1fb52f-normal/` |
| Global parent-filter attribution | `empty` retained zero BPF runs; PASS and SELECT retained about one run per operation and about 25 ns per BPF run | `results/experiments/fxmark-rq2-preflight/20260727T-parent-fast-path-8fd1fb52f-stats/` |
| Global parent-filter implementation | Kernel `8fd1fb52f`; complete Phase 1 and policy-semantic KVM gates passed | `docs/tmp/2026-07-27-namei-ext-global-parent-fast-path-implementation.md` |
| RCU target-registry repair | Kernel `83d52c216`; lockless target reads improved directional SELECT throughput by about 5.9% but did not alone pass the FUSE gate | `docs/tmp/2026-07-27-namei-ext-rcu-target-selection-design.md` |
| RCU borrowed-target repair | Kernel `bdc9a83e3`; forced `RESOLVE_CACHED`, concurrent atomic replacement, complete Phase 1, independent lifetime review, and final repeated matrix all passed | `docs/tmp/2026-07-27-namei-ext-rcu-target-selection-implementation.md` |

Formal-v3 and the isolated fast-path confirmation form the current paper-level
cache-hot `stat()` mechanism result. Formal-v3 supports the scoped
SELECT-over-cached-FUSE claim and quantifies active-policy cost. The separate
30-block confirmation supports the unused-fast-path criterion on this host at
one, two, and four workers. It does not prove zero overhead or generalize to
active policy, other operations, cold caches, tails, or other machines.
Earlier matrices and short preflights remain internal mechanism evidence.

The reconsidered mdtest cold and mutating metadata branch closed as incomplete
after its three permitted real KVM preflights. In the final attempt, stock,
patched-unattached, `PASS`, and `SELECT` completed 24/24 declared cells, but the
controller found the guest hard open-file limit at the kernel's initial 4,096
value and rejected the FUSE child before mounting. The full
five-condition matrix is incomplete, formal execution is not authorized, and
no number from these roots is paper evidence. The final execution record is
`docs/tmp/2026-08-02-rq2-mdtest-reconsideration-preflight-attempt-3.md`.
Cache-cold and broader mutating-metadata cost therefore remain unresolved; this
closed RQ2 branch remains excluded and is not counted among the seven RQ1
workload cases.

### J. Agent workspace ownership and responsibility (decisive RQ3 result)

| Cell | Status | Raw root |
| --- | --- | --- |
| Matched semantic contract | Passed in three independent KVM boots: `namei_ext` and a Linux 7.1 port of official Wrapfs commit `464802c8fd1a25413b295161c9bb9a4ce7bfa33b` each passed all 37/37 pairwise AgentFS-derived oracles over the same ext4 lower tree | `results/experiments/agent-workspace-rq3-formal/20260728-rq3-formal-v3/` |
| Lower-file operation boundary | Passed 3/3: after selection, policy detach, and child-cgroup removal, read/write/fsync/fstat/fchmod used the already-open lower file and did not increment the BPF counter | Same raw root |
| Stackable method attribution | Passed 3/3: 13 probed Wrapfs method classes executed, covering superblock setup/teardown, lookup, readdir, open, read, write, fsync, getattr, setattr, create, rename, and unlink | Same raw root |
| Fail-closed matrix | Passed 3/3: two verifier-rejected programs plus 19 independently loaded malformed or unsupported runtime decisions; every runtime cell returned the declared errno and preserved statx metadata for eight lower objects plus exact manifests for two directories | Same raw root |
| Deployed-source accounting | Nine `namei_ext` kernel integration files; six compiled Wrapfs sources with 34 unique VFS slots; 12 userspace FUSE callbacks and 15 compiled kernel FUSE client sources | Same raw root and `report.md` |
| Result review | Valid for a scoped method and runtime-responsibility claim; not a claim that custom filesystems are unnecessary | `docs/tmp/2026-07-28-agent-workspace-rq3-formal-v3-result-review.md` |

Make entrypoint:
`make experiment-agent-workspace-rq3 RUN_ID=<fresh-id>`.

The supported RQ3 answer is workload-specific. For this existing-object Agent
workspace view, policy execution is confined to lookup and directory iteration,
and ordinary operations continue on the selected lower file. The matched
stackable implementation owns and executes a broader filesystem-method surface.
Invalid programs and unsupported outputs failed at the verifier or declared
errno boundary in the tested matrix. The ownership result does not cover
synthetic contents, copy-up, conflict resolution, distributed metadata, or
arbitrary filesystem behavior.

## Open Questions

1. Does selecting an existing file or directory reconnect to ordinary VFS
   semantics across a standard filesystem conformance suite, rather than only
   across the operations exercised by the current workload oracles?
2. What are the setup and steady-state costs of the W3 action view relative to
   Bazel's symlink-forest behavior and a matched FUSE view after the real Bazel
   correctness preflight?
3. Does the completed W6 final-object result retain its cost advantage under a
   matched FUSE implementation and a larger Pynamic/MPI launch, while Spindle
   still owns distribution and cache population?
4. Does the existing ccache macro ratio survive independent-run
   median/dispersion reporting and a hardened FUSE configuration with caching
   and passthrough explicitly accounted for?
5. Does the Agent workspace RQ3 boundary result generalize to a second
   source-derived traditional workflow without requiring broader filesystem
   semantics?

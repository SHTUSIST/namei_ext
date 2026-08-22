# Experiment Plan: RQ2 Spindle Final-Object Selection Versus FUSE

## Research Question

- RQ exactly as written in the paper: What is the cost of putting programmable
  policy on the VFS name-resolution path compared with a feature-equivalent
  FUSE policy implementation?
- Specific uncertainty tested here: On a real dynamic-loader workload whose
  node-local objects and pathname mappings were produced by LLNL Spindle, does
  in-VFS selection reduce warm loader completion time and system service work
  relative to an optimized FUSE view over the same 47 objects?
- Why the answer matters: Existing RQ2 results contain standard VFS
  microbenchmarks and one Agent workspace macro workload. A second,
  non-Agent, source-derived comparison is needed to show that the measured
  difference survives an unchanged traditional application and its real
  loader access pattern.

## Paper-Value Admission

- Planned role: decisive.
- Largest credible paper story this experiment could unlock: `namei_ext` can
  preserve the completed Spindle loader behavior while avoiding the daemon
  request path and runtime cost of a feature-equivalent FUSE view in a
  traditional HPC software-staging workload.
- Strongest reviewer reject argument or load-bearing uncertainty addressed:
  the large Agent lifecycle speedup may be specific to that custom lifecycle,
  while FxMark may not predict end-to-end behavior under a real loader and
  warm caches.
- Independent evidence added beyond existing runs and published results: the
  exact 47-object Spindle mapping, unchanged `test_driver`, and first-party
  loader transcript are exercised under both mechanisms with paired macro
  timing, mechanism-engagement counts, and resource attribution. Neither the
  completed W6 RQ1 result nor the existing FUSE literature supplies this
  matched comparison.
- Why the result is not tautological, already settled, or dominated: optimized
  FUSE can cache dentries, attributes, and file data in the kernel, so the
  loader can plausibly make the two mechanisms indistinguishable after warmup.
  The FAST'17 FUSE study and RFUSE also show that FUSE overhead depends strongly
  on workload and caching; a generic citation cannot settle this workload.
- Paper decision if positive: use this as the traditional source-derived RQ2
  macro result beside Agent workspace and FxMark, scoped to warm final-object
  selection after Spindle cache population.
- Paper decision if contradictory, mixed, or inconclusive: retain the complete
  W6 RQ1 result, but do not claim a Spindle macro-performance advantage; use
  the request and resource data to explain whether kernel caching removed the
  daemon path or whether another loader cost dominated.
- Best alternative experiment and why this one has higher decision value:
  reopening W3 Bazel/sandboxfs would directly use a source FUSE system, but
  its frozen protocol exhausted three real preflight attempts without a valid
  paired run. W7 would require a new generic view with a weaker source-native
  access trace. W6 reuses a completed exact mapping and oracle and therefore
  has the highest probability of producing a fair traditional-workload answer
  without weakening the baseline.

This RQ2 experiment does not merge, replace, or remove any of the seven
mandatory RQ1 case studies W1--W7.

## Expected And Alternative Outcomes

- Current expected answer: both mechanisms pass the same application oracle;
  the boot-paired FUSE/namei_ext median loader-time ratio is above one with a
  95% confidence interval that excludes one, and FUSE consumes measurable
  daemon CPU and callbacks during the measured window.
- Strongest competing explanation: after warmup, FUSE's dentry, attribute,
  and page-cache state may satisfy almost all loader work in the kernel, while
  relocation and symbol resolution dominate total time; the paired macro
  runtime may therefore be equal even though microbenchmarks differ.
- Result that would contradict the expectation: the upper endpoint of the
  paired 95% confidence interval is below one while both conditions pass every
  correctness and mechanism-engagement check. An interval spanning one is
  mixed/inconclusive for the macro timing claim.

## Published Precedent And Real Assets

- Closest published protocol: Frings et al., *Massively Parallel Loading*
  (ICS'13), measure application launch/loading time and filesystem operations
  for Spindle/Pynamic. Vangoor et al., *To FUSE or Not to FUSE* (FAST'17), use
  an optimized multithreaded passthrough filesystem with kernel caching,
  splicing, and resource/request attribution. Cho et al., *RFUSE* (FAST'24),
  report operation latency/throughput and CPU use and demonstrate that cached
  and sequential FUSE workloads can approach lower-filesystem performance.
- Official system/model/data/benchmark/tool and version: LLNL Spindle commit
  `8853636d2d77`; its unmodified `testsuite/test_driver`; libfuse 3.18.2
  commit `033844748010a3b8265bf1c90b9ae8ffe4cd9ca7`; project modified Linux
  7.1-rc7 with `CONFIG_FUSE_PASSTHROUGH=y`, booted in KVM.
- What is reused: Spindle serial-pull cache population, first-party
  global-to-local logs, the 47 exact staged objects, the 44-line loader
  progress transcript, existing W6 mapping/preservation checks, and
  libfuse's passthrough and cache configuration patterns.
- Necessary deviations or custom glue: a read-only low-level libfuse3 adapter,
  derived from the official `passthrough_ll`/`passthrough_hp` patterns, maps the
  47 exact logical relative paths to Spindle's existing cache files and passes
  every other path through to a hidden bind of the original test tree. It
  enables the upstream kernel FUSE passthrough API for opened regular files,
  adds per-callback and per-target counters, and explicitly notifies the kernel
  about the affected entry and inode when permission or withdrawal state
  changes. It does
  not populate or transform file data.

## Comparison

- Proposed system or method: `namei_ext` with the existing
  `spindle_staging.bpf.c` exact-parent policy and the same 47 registered cache
  targets.
- Main baseline and competing position it represents: optimized,
  multithreaded libfuse 3.18.2 low-level read-only filesystem with long-lived
  entry/attribute caching, `default_permissions`, splice capability, and
  upstream kernel FUSE passthrough for regular-file read and `mmap`. It
  represents implementing the same final-object selection as a userspace
  filesystem service while removing avoidable userspace data-copy overhead.
- Why the main baseline needs a matched run instead of citation alone: FUSE
  studies do not execute this Spindle-produced object set, loader transcript,
  or cache-hot operation mix, and published results explicitly show that FUSE
  overhead is workload dependent.
- Controls or ablations, labeled separately: original Spindle serial-pull is
  the source oracle and cache-population control, not a performance baseline.
  Per-target BPF hits and FUSE backend-open counters establish mechanism
  engagement. A withdrawn `libtest10.so` mapping and a target-mode permission
  probe establish the same failure behavior. Direct lower manifests establish
  preservation.
- Conclusion if the main baseline matches or wins: the W6 workload supports
  expressiveness but not a macro-cost advantage; RQ2 performance remains
  supported only by Agent workspace and standard VFS benchmarks for their
  tested scope.
- Information, tuning, and compute fairness: both mechanisms receive the same
  47 source-to-cache mapping before execution, use the same logical root,
  cache files, argv, environment, uid/gid, KVM resources, warmup count, and
  measured launches. The FUSE view enables all applicable upstream caching and
  multithreading options. Each loader child moves into a condition-specific
  sibling cgroup inside the timed window; only the `namei_ext` sibling has the
  policy attached, so cgroup migration cost is common to both conditions. No
  source file is copied into the view. Condition order alternates by boot.
- Split or leakage rule when relevant: each paired block contains two fresh
  boots, one per condition. Spindle cache population is rerun inside every
  boot. Odd blocks run namei_ext then FUSE; even blocks reverse that order.
  Each condition receives its own clean kernel, mount/policy setup, and warmup,
  so dentry/page-cache state cannot leak between mechanisms. The primary
  statistical unit is the paired block, not the individual launch.

## Workloads And Metrics

- Real workload: the unmodified Spindle `testsuite/test_driver` dynamically
  loads the exact staged libraries through its compiled logical test path.
- Primary metric: per-boot median warm loader completion time in nanoseconds;
  report the geometric mean of the ten paired FUSE/namei_ext ratios and a
  paired 95% bootstrap confidence interval over boot-level log ratios.
- Secondary metrics: p50/p95 launch time from raw samples; client user/system
  CPU from `wait4`; FUSE daemon user/system CPU, scheduler runtime, and
  voluntary/involuntary context switches over the same measured window; total
  callbacks; per-target backend opens; `namei_ext` policy invocations and
  selections; and mechanism setup/teardown time. Secondary metrics explain the
  primary effect and do not override it.
- Correctness check or ground truth: original Spindle exits zero and creates
  47 byte-identical cache mappings; every warmup and measured launch exits zero
  with the exact 44-line stderr transcript; all 47 targets are engaged by each
  mechanism; logical stat size/type/mode matches its selected cache object;
  `libtest10.so` withdrawal causes the expected loader failure; non-root open
  observes `EACCES` when the selected cache object's mode is zero; source and
  cache metadata and bytes required by the W6 oracle remain unchanged after
  restoration and cleanup. The FUSE arm additionally requires successful
  negotiation of `FUSE_CAP_PASSTHROUGH`, a positive backing ID for every
  regular-file open in the measured window, zero userspace read fallback, and
  zero from every inode notification. Each entry notification must return zero
  or `-ENOENT`; the kernel reverse-invalidation implementation uses the latter
  when it cannot find a parent or positive parent/name cache entry. This status
  is not sufficient by itself. When it occurs, the FUSE arm must successfully
  issue the mainline connection-epoch notification before continuing; the raw
  result records both the fallback attempt and its status. A subsequent
  non-root `fstatat` of the withdrawn pathname must return `ENOENT`, the
  withdrawn loader must fail with its exact diagnostic, and the backing-object
  engagement counter must not advance. Any other notification error, failed
  epoch fallback, or stale pathname observation invalidates the run. The
  namei_ext arm performs the equivalent
  transition by replacing the selected-target rule with an explicit `HIDE`
  rule; deleting the rule would mean `PASS` and expose the lower source file.
  Its lookup-hide counter must advance while the selected-target hit counter
  remains unchanged.
- Repetitions, seeds, and uncertainty: formal run uses 20 fresh modified-kernel
  KVM boots arranged as ten condition pairs, with three warmups and 50 measured
  loader launches in each boot. Odd pairs launch namei_ext then FUSE; even
  pairs reverse the order. The timed interval begins immediately before fork
  and ends at blocking `wait4`; timeout enforcement is separate and does not
  poll at 10 ms granularity. Transcript parsing and result emission occur after
  timing. The bootstrap uses a committed deterministic seed and resamples the
  ten paired-block log ratios, not the 500 within-condition launches.
- Cost estimate when material: approximately 1,000 measured loader launches,
  60 warmups, 20 source-population runs, and 20 KVM boots; expected wall time is
  below one hour on the current host.

## Planned Runs

| Run group | Role | Workload | System/method | Repetitions | Decision consequence |
|---|---|---|---|---:|---|
| source | control | Spindle test-driver cache population | Original Spindle serial pull | 1 per boot | Must produce the same 47 valid mappings before comparison |
| paired main | proposed | Warm Spindle loader | `namei_ext` exact selection | 10 boots x 50 launches | Proposed side of ten independent condition pairs |
| paired main | baseline | Warm Spindle loader | Optimized libfuse3 exact view with kernel passthrough | 10 boots x 50 launches | Strongest runnable same-oracle FUSE comparison |
| oracle | control | Permission and withdrawn target | Both mechanisms | 1 each per boot | Vetoes timing if failure behavior is not equivalent |

## Execution

- Authoritative command or workflow: `make kvm-spindle-staging-rq2 RUN_ID=<id>`.
- Real preflight case: `make kvm-spindle-staging-rq2-preflight RUN_ID=<id>`
  performs one two-boot condition pair on the modified kernel: source
  population and one real mechanism per boot, one warmup and five measured
  launches per condition, withdrawal, permission, preservation, and teardown.
- Full completion rule: all 20 boots complete; both mechanisms pass every
  declared correctness and cleanup oracle; exactly 500 valid measured samples
  exist per condition; every target has positive mechanism engagement in every
  boot; the analyzer recomputes the paired effect and confidence interval from
  raw samples and emits a terminal supported/contradicted/inconclusive result.
- Raw-result path:
  `results/experiments/spindle-staging-rq2/<RUN_ID>/`.
- Checkpoint or recovery approach: every boot writes to its own directory and
  the owning target fails on the first invalid boot. Failed or completed result
  roots are retained and never reused; a repaired protocol runs under a new
  `RUN_ID`.

## Interpretation

- Positive result: both conditions pass, all mechanism checks engage, and the
  paired 95% interval for FUSE/namei_ext loader time is entirely above one.
- Negative or contradictory result: both conditions pass and the interval is
  entirely below one.
- Mixed or inconclusive result: the interval includes one after all runs pass.
  Correctness, baseline-engagement, cleanup, or fairness failures make the run
  invalid and require repair plus a fresh result root; they are not statistical
  evidence and cannot be labeled inconclusive timing.
- Target paper figure or table: one paired per-boot dot/interval plot for
  loader time plus a compact row for daemon CPU, callbacks, and BPF decisions;
  combine it with the Agent macro and FxMark RQ2 evidence rather than creating
  another independent evaluation story.

## Execution-Grounded Amendment

Formal02 and formal03 showed that the low-level entry notification reproducibly
returns `-ENOENT` while inode notification succeeds. Reordering notifications
did not change this result. The original exact-zero entry gate was therefore
checked against the pinned primary source. libfuse 3.18.2
passes the low-level notification to the kernel and returns its error. Linux
`fs/fuse/dir.c:fuse_reverse_inval_entry()` returns `-ENOENT` when the parent or
positive parent/name cache entry is absent. The official low-level API does not
define `-ENOENT` itself as proof of a completed state transition.

The plan now preserves the raw status and admits only zero or `-ENOENT` for the
entry notification. It does not infer correctness from that status. Every
condition and repetition must also contain a non-root permission result, a
withdrawn-path `fstatat` result of `ENOENT`, an exact withdrawn-loader failure,
and a no-backing-engagement window. These checks veto the complete run if stale
lookup state survives or FUSE rejects only at `open`. This is the first plan
amendment; the workload, primary metric, paired design, sample budget, cache
configuration, and expected outcomes are unchanged.

## Reproducibility Notes

- Software and data versions: Spindle `8853636d2d77`, libfuse 3.18.2
  `033844748010a3b8265bf1c90b9ae8ffe4cd9ca7`, and the exact project/kernel
  commits are recorded in raw metadata.
- Config and seed notes: four KVM vCPUs pinned through the existing verified
  affinity path; deterministic bootstrap seed; fixed warmup and sample counts.
- Known deviations: this is one-node final-object selection after Spindle has
  populated node-local cache files. It does not evaluate Spindle distribution,
  cache population scalability, or parallel launch scale.

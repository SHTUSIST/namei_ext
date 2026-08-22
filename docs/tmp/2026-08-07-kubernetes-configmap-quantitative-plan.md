# Experiment Plan: W4 Source-Relative Quantitative Extension

## Research Question

- RQ exactly as written in the paper: **Can a narrow VFS name-resolution
  extension express real state-dependent path-view policies without taking
  over filesystem semantics?**
- Specific uncertainty tested here: The completed W4 result establishes
  source-oracle correctness for an already-materialized Kubernetes projected
  payload, but gives no quantitative evidence about the work needed to prepare,
  publish, consume, and roll back that view. This experiment asks whether
  `namei_ext` reduces the measured lifecycle cost and filesystem-object churn
  for that exact subset relative to Kubernetes v1.30.0 `AtomicWriter`.
- Why the answer matters: Without matched quantitative evidence, W4 shows only
  that the policy is expressible. It does not show that separating selection of
  existing objects from payload publication has practical value.

This is a source-relative quantitative extension to W4 and RQ1. It does not
replace the paper's RQ2 comparison against feature-equivalent FUSE or its RQ3
comparison against a stackable filesystem.

## Paper-Value Admission

- Planned role: supporting.
- Largest credible paper story this experiment could unlock: For a
  two-known-generation, post-acknowledgement service-configuration selection
  lifecycle, `namei_ext` preserves the completed source oracle while requiring
  less publication work than the source system's materialized view as the
  projected payload grows.
- Strongest reviewer reject argument or load-bearing uncertainty addressed:
  W4 currently counts passed states but does not quantify setup, update,
  rollback, lookup, or filesystem-object cost. The reviewer can reasonably
  dismiss it as a functional demonstration with no practical advantage.
- Independent evidence added beyond existing runs and published results: The
  existing W4 result contains no durations, scaling points, or
  cumulative object/materialization observations. Kubernetes publishes the
  `AtomicWriter` algorithm and tests, but does not publish a matched benchmark
  for this four-state lifecycle.
- Why the result is not tautological, already settled, or dominated: The main
  result includes `namei_ext`'s per-volume lower-object preparation, logical
  placeholders, target registration, policy-map population, state changes,
  and application observations. It does not time only a generation-map update.
  `namei_ext` can therefore lose if its setup and lookup costs exceed the work
  avoided during update and rollback.
- Paper decision if positive: Add one source-relative W4 quantitative figure
  and state the advantage only for the already-materialized payload-view
  subset and measured lifecycle. Do not generalize to kubelet propagation,
  inotify, service reload, or all projected volumes.
- Paper decision if contradictory, mixed, or inconclusive: Keep W4 as RQ1
  correctness breadth. Report no source-relative cost advantage. A crossover
  supports only the payload sizes above the measured crossover.
- Best alternative experiment and why this one has higher decision value: A
  second W3 FUSE attempt would revisit a just-closed protocol whose readdir
  oracle was invalid. W4 already has a reviewed official-source adapter and a
  complete four-state oracle, so one quantitative extension adds new evidence
  without changing the workload or inventing another baseline.

## Expected And Alternative Outcomes

- Current expected answer: `namei_ext` has higher one-time control-plane setup
  but lower V1 publication, repeated V1 no-op, and rollback cost. Its complete
  lifecycle should become cheaper as payload width grows because the two
  existing generations are prepared once, whereas a changed `AtomicWriter`
  publication writes a new timestamped payload and updates symlink topology.
- Strongest competing explanation: Target registration and two BPF map entries
  per pathname, plus policy execution on every application lookup and readdir,
  cost as much as or more than `AtomicWriter`'s materialization on the tested
  guest filesystem.
- Result that would contradict the expectation: The paired complete-lifecycle
  ratio is at or above 1.0 at 256 entries with a confidence interval that does
  not support a `namei_ext` advantage, or any correctness oracle differs.

## Published Precedent And Real Assets

- Closest published protocol: Kubernetes v1.30.0
  [`AtomicWriter.Write()`](https://github.com/kubernetes/kubernetes/blob/7c48c2bd72b9bf5c44d21d7338cc7bea77d0ad2a/pkg/volume/util/atomic_writer.go)
  and its upstream tests define validation, change detection, timestamped
  payload creation, atomic `..data` replacement, visible symlink maintenance,
  removed-path cleanup, and old-generation removal. The completed W4 protocol
  already derives V0, V1, repeated V1, and V0 rollback from this API.
- Official system/model/data/benchmark/tool and version: Kubernetes v1.30.0,
  commit `7c48c2bd72b9bf5c44d21d7338cc7bea77d0ad2a`, built from the pinned upstream
  vendor tree; the repository's modified Linux kernel; and the real
  `cgroup/namei_ext` attach path in KVM.
- What is reused: The official Go package, existing W4 payload semantics,
  non-root consumer, exact-byte and mode checks, lookup/readdir membership,
  stable-root `openat()`, old-file-descriptor behavior, direct-lower identity,
  lower-object preservation, policy counters, and cleanup gates.
- Necessary deviations or custom glue: Extend the existing official-source
  adapter and `namei_ext` runner with generated flat ConfigMap entries,
  monotonic phase timing and raw object/materialization
  counts. One common persistent consumer executable performs the timed
  application operations in both conditions. No part of `AtomicWriter` is
  copied or reimplemented.

There is no published Kubernetes benchmark that settles this matched question.
The official API and source algorithm fix the baseline boundary; the measured
numbers come from the shared KVM protocol rather than from an unrelated
filesystem benchmark.

## Comparison

- Proposed system or method: `namei_ext` selects registered V0 or V1 lower
  objects under one stable projected-volume pathname and uses `HIDE` for names
  absent from the selected generation.
- Main baseline and the competing position it represents: The unmodified
  Kubernetes v1.30.0 `AtomicWriter` is the one main baseline. It represents the
  source system's materialized projected-volume implementation.
- Why the main baseline needs a matched run instead of citation alone:
  Upstream source establishes which operations occur, but does not give cost on
  this kernel, guest filesystem, payload, application observation sequence, or
  four-state lifecycle.
- Controls or ablations, labeled separately: Direct V0/V1 reads and inventories
  are correctness controls. A one-time `namei_ext` program load/attach timing is
  reported separately and is not silently amortized into or omitted from the
  result. A trace-only diagnostic run may count mutating syscalls, but is not a
  timing baseline and is not required for the main result.
- Conclusion if the main baseline matches or wins: W4 provides expressiveness
  evidence but no demonstrated source-relative cost advantage for this
  lifecycle. The paper must not infer a general advantage from the source
  algorithm alone.
- Information, tuning, and compute fairness: Complete V0 and V1 payload inputs
  are available in memory before either timer begins. Both conditions run under
  the same fixed non-root identity, vCPU allocation, guest filesystem, parent
  directory, and fresh per-sample roots. Payload generation and process startup
  are outside both timed regions. Each condition includes all of its own
  per-volume setup and publication work. Pair order alternates AB/BA within each
  boot and starting order alternates across boots. Scale order rotates across
  boots. Neither condition receives a cache flush or a private warmup
  unavailable to the other.
- Split or leakage rule when relevant: No training or tuning exists. Scale,
  repetitions, ordering, and metrics are frozen before the real preflight.

The comparison is feature-equivalent only for the completed W4 contract:
post-acknowledgement lookup, open, stat, readdir, stable-root `openat()`, old
descriptors, update, no-op, and rollback when both complete generation inputs
are known before the lifecycle starts. ConfigMap retrieval, previously unseen
generation arrival, cross-open snapshots, symlink topology, inotify,
application validation/reload, and kubelet propagation remain outside both the
performance claim and the `namei_ext` condition.

## Workloads And Metrics

- Real workloads or tasks: One Kubernetes projected payload follows V0 initial
  publication, V1 update, repeated V1 no-op, and V0 rollback. Payload width `N`
  is the union of projected leaf pathnames across V0 and V1. Each generation
  contains `N-1` regular files. The four canonical union paths are
  `config/app.conf`, `tls/cert.pem`, `retired.conf`, and `added.conf`: V1 changes
  the first two, removes the third, and adds the fourth. The remaining `N-4`
  paths are flat `entry-%03u.conf` files retained unchanged with bytes
  `stable-%03u\n` and mode 0644. `N` is 4, 16, 64, or 256, so four of `N` union
  paths change: 100%, 25%, 6.25%, and 1.5625%. `N=4` is the canonical completed
  W4 payload; `N=256` is a disclosed scaling endpoint imposed by the existing
  policy-map capacity, not a claim that 256 files is the median ConfigMap.
- Primary metric: Per-boot median ratio of complete acknowledged lifecycle
  wall time (`wall_span_ns`), `namei_ext / AtomicWriter`, at 256 entries. The lifecycle begins with an
  existing fresh per-sample parent under which the condition target root,
  logical/lower roots, and per-volume cgroup do not yet exist and before any
  condition-specific filesystem or BPF-map mutation. It ends after the common
  consumer acknowledges the rollback state. For
  `AtomicWriter`, the timer includes target-root creation, writer initialization,
  all four `Write()` calls, and the consumer after each call. For `namei_ext`, it
  includes per-volume cgroup/root creation, both lower-generation directory and
  file writes with modes/ownership, logical placeholders, target registration,
  both view-map populations, four generation selections, and the same consumer
  after each selection. It excludes common in-memory payload construction,
  process startup, one-time program load/attach, raw-result encoding, exhaustive
  post-state validation, and teardown. Program load/attach is reported
  separately; validation and teardown remain hard gates. The sum of the nine
  individually timed setup, publication, and consumer phases is retained as a
  decomposition check, not substituted for the begin-to-end primary timer.
- Filesystem substrate: Each KVM boot receives one fresh 1 GiB raw image stored
  on the host's ext4 filesystem and attached directly as a virtio block device
  with QEMU `cache=none`. The guest identifies the device by its fixed serial,
  formats it as ext4, mounts it, and creates every `AtomicWriter` and
  `namei_ext` sample parent below that mount. This matches ordinary ConfigMap
  volume's non-memory `EmptyDir` path rather than changing the source workload
  to a projected volume, which requests memory-backed storage. After the common
  mount crossing, all sample-tree operations use ext4 and its block I/O does
  not use the guest's 9p, overlay, or tmpfs backing. Host backing, guest block
  identity, `findmnt`, `blkid`, and
  `statfs` are retained. Guest unmount and mountpoint removal and host image
  removal are separate hard gates. Raw JSONL and logs are written to the result
  root only outside the primary timer. Performance conclusions are scoped to
  this raw-virtio/ext4 setup.
- Timed consumer sequence: The same persistent consumer executable is used by
  both mechanisms. At every state it enumerates the projected root and nested
  directories, opens, reads, and stats every expected present leaf, checks the
  one absent union leaf, and performs `openat()` through the directory descriptor
  retained from initial publication. It retains `config/app.conf` from V0 and
  verifies that descriptor after later states. The timer covers these operations
  and their semantic comparisons plus a bounded three-field acknowledgement,
  but not the full O(N) evidence serialization or tree inventories. The
  consumer sends that acknowledgement immediately after each state and
  emits the full per-file JSON evidence only after the rollback timer has
  stopped. Raw operation counts require both conditions to execute the same
  sequence before their timing rows are admitted.
- Secondary metrics: Complete-lifecycle ratio at 4, 16, and 64 entries;
  wall-clock time for per-volume setup, initial state, update, no-op, rollback,
  and fixed application observations; publication-only
  lifecycle sum (four `AtomicWriter.Write()` calls versus four generation-map
  updates); one-time BPF load/attach cost; directly observed regular files and
  bytes in every newly published AtomicWriter timestamp tree; directly observed
  V0/V1 lower files and bytes prepared by `namei_ext`; and live logical-root
  object inventory after each state. AtomicWriter materialization inventories
  are captured in separate, fresh, untimed audit lifecycles only after the
  entire boot's AB/BA timing matrix has finished, so their writes and directory
  walks cannot alter any timed condition. Each audit inventory is captured after its `Write()` and before the
  next audit call can remove that timestamp tree. The no-op must retain the same
  `..data` target and contribute no newly materialized timestamp tree. Cumulative symlink-create
  counts are not claimed without a successful untimed mutation trace.
- Correctness check or ground truth: Every measured lifecycle must pass the
  completed W4 exact-byte, mode, visible-name, non-root read, direct-lower
  identity, stable-root `openat()`, old-fd, no-op identity, rollback,
  lower-preservation, policy-counter, attachment, cleanup, and dmesg oracles.
  Generated entries add exact bytes, modes, membership, and full-count checks.
  Exhaustive inventories, direct-lower controls, counter checks, dmesg, raw
  result encoding, and cleanup execute outside the primary timer.
  Any failed oracle invalidates that lifecycle and fails the owning run; failed
  samples are not filtered.
- Repetitions, seeds, and uncertainty: Twenty fresh KVM boots. Each boot executes
  five paired lifecycles at each of four scales, alternating condition order.
  Odd boots use AB, BA, AB, BA, AB; even boots use BA, AB, BA, AB, BA. Scale
  order rotates by boot. The analysis first computes one median log-ratio per
  boot and scale, then bootstraps those 20 independent boot-level values and
  exponentiates the median and 95% interval. Superiority requires the transformed
  upper 95% limit to be below 1.0. Raw per-phase observations remain available.
  Samples from one boot are not treated as independent boots.
- Host controls and provenance: Four vCPUs are pinned one-to-one to host CPUs
  4--7 and independently read back through QMP before each guest workload. The
  result records host CPU topology, frequency policy, selected CPUs,
  `vng --version`, and `/proc/stat` and `/proc/interrupts` before and after the
  multi-boot run. These fields are execution provenance, not checksum gates.
- Cost estimate when material: 20 formal KVM boots plus one paired preflight.
  Each boot executes 40 lifecycles. The payload limit is 256 because the current
  workload policy maps admit 256 component entries per generation; this limit
  is disclosed rather than changed for the experiment.

## Planned Runs

| Run group | Role | Workload | System/method | Repetitions | Decision consequence |
|---|---|---|---|---:|---|
| source-positive | main baseline | V0/V1/no-op/rollback at 4, 16, 64, 256 entries | Kubernetes v1.30.0 `AtomicWriter` | 20 boots x 5 per scale | Fixes source correctness and materialized-view cost |
| proposed | proposed | Same payload, states, identity, and application observations | real KVM `cgroup/namei_ext` | 20 boots x 5 per scale | Tests W4 lifecycle cost and scaling |
| direct-lower | control | V0/V1 objects before and after each lifecycle | direct lower filesystem | every lifecycle | Gates identity and preservation; no performance claim |
| unmanaged | control | logical placeholder view outside the attached process group | ordinary VFS | once per boot | Gates mechanism scope; no performance claim |
| trace diagnostic | diagnostic | one 64-entry paired lifecycle | both main conditions | one preflight-only capture if supported | Explains mutating syscall mix; never substitutes for main timing |

## Execution

- Authoritative command or workflow:
  `make kvm-kubernetes-configmap-quantitative-preflight RUN_ID=<id>` followed,
  only after a GO result review, by
  `make experiment-kubernetes-configmap-quantitative RUN_ID=<id>`.
- Real preflight case: One fresh modified-kernel KVM boot executes two paired
  lifecycles at both 16 and 256 entries in counterbalanced order. It must reach
  the real BPF attach,
  official `AtomicWriter.Write()`, every four-state oracle, timing capture, raw
  result write, cleanup, and host-side analysis path.
- Full completion rule: All 20 boots and all 800 condition-lifecycle rows
  complete; every
  declared correctness and cleanup gate passes; both condition orders appear in
  every boot; every scale has five pairs per boot; host provenance and vCPU
  placement are complete; and the independent claim-to-code-to-raw-evidence
  review returns GO.
- Raw-result path:
  `results/experiments/kubernetes-configmap-quantitative/<RUN_ID>/`.
- Checkpoint or recovery approach: Result roots are immutable. A failed or
  interrupted preflight or formal run remains raw failure evidence. Any rerun
  uses a new `RUN_ID`; no stored guest Makefile or result is repaired or reused.

## Interpretation

- Positive result: All correctness gates pass and the 256-entry complete
  lifecycle ratio has a transformed upper 95% confidence limit below 1.0.
  Report the exact ratio, interval, absolute times, phase decomposition, and
  object/materialization counts. Scope the statement to W4's measured
  already-materialized payload-view contract.
- Negative or contradictory result: Any correctness failure rejects the
  experiment. Correct runs with an AtomicWriter match or win reject the W4
  source-relative cost claim; retain only the existing W4 sufficiency result.
- Mixed or inconclusive result: Report a measured crossover only at supported
  scales, or no cost conclusion if the interval spans parity. A faster state
  switch with a slower complete lifecycle does not support an overall
  lifecycle advantage and must be shown as phase decomposition rather than
  promoted to the main result.
- Target paper figure or table: One log2-x line plot with absolute complete
  lifecycle time for both systems and boot-clustered 95% intervals, plus a
  compact 256-entry phase decomposition. A small adjacent table reports lower
  regular files/payload bytes and live visible namespace objects after each
  state. RQ1's
  seven-case correctness table remains unchanged.

## Reproducibility Notes

- Software and data versions: Kubernetes v1.30.0 at
  `7c48c2bd72b9bf5c44d21d7338cc7bea77d0ad2a`; repository and kernel commits,
  kernel release, Go version, compiler, libbpf/bpftool, guest filesystem, vng
  version, and host CPU identity recorded in raw metadata.
- Config and seed notes: Deterministic payload names and bytes; fixed scales and
  repetition count; AB/BA order determined by repetition parity; fixed vCPU
  set. Bootstrap analysis uses a committed deterministic seed and resamples
  boots, not individual within-boot lifecycles.
- Known deviations: The benchmark uses the official `AtomicWriter` package but
  not a running kubelet. Payload retrieval and application reload are outside
  the tested boundary. The 256-entry ceiling is the existing BPF policy-map
  capacity, not a Kubernetes limit. No checksum manifests or checksum gates are
  added or used.

# W4 Kubernetes ConfigMap Quantitative Formal Result Review

## Question And Role

This supporting RQ1 experiment asks whether `namei_ext` adds practical value
to the already-completed W4 correctness case. The tested subset has two
complete, known generations and executes initial publication, update, repeated
no-op, and rollback under one stable volume root. The matched baseline is the
official Kubernetes v1.30.0 `AtomicWriter` implementation.

The predeclared primary metric is the paired complete per-volume lifecycle at
256 union paths. The `namei_ext` timer includes lower-object and placeholder
creation, per-volume cgroup creation, target registration, policy-map
population, four generation selections, and the same persistent consumer used
by the baseline. Program load and attachment are measured separately because
one program attached at the cgroup root can serve multiple per-volume cgroup
IDs.

## Formal Run

- Command: `make kvm-kubernetes-configmap-quantitative
  RUN_ID=20260808T142207Z-w4-quantitative-formal01`
- Raw root:
  `results/experiments/kubernetes-configmap-quantitative/20260808T142207Z-w4-quantitative-formal01/`
- Source commit: `1f529c3a00d58cea26a0acea7a2f1b0c768fab9e`,
  clean.
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`,
  clean.
- Source baseline: Kubernetes v1.30.0 at
  `7c48c2bd72b9bf5c44d21d7338cc7bea77d0ad2a`; the pinned
  `TestWriteOnce`, `TestUpdate`, and `TestMultipleUpdates` tests passed.
- Matrix: 20 fresh modified-kernel KVM boots, five AB/BA pairs at each of
  4, 16, 64, and 256 paths, for 800 lifecycle rows.
- Filesystem: a fresh 1 GiB raw virtio block device, formatted as ext4, per
  boot; QEMU used `cache=none`.
- CPU placement: four guest vCPUs were pinned and read back on host CPUs 4--7
  before each workload.

The Make target, finalizer, captured validator, and captured analyzer all
returned zero. An independent streaming recomputation then checked every
boot's event counts, AB/BA order, setup additivity, registered-target count,
mechanism status, host and guest block cleanup, workload stderr, and vCPU
placement. It reproduced every reported center and confidence interval.

## Correctness Evidence

All 238,800 raw observations passed:

| Evidence | Count |
| --- | ---: |
| Complete source or `namei_ext` lifecycles | 800 |
| Selected-object and unmanaged identity observations | 170,000 |
| Lower-object preservation observations | 67,200 |
| Independent AtomicWriter materialization audits | 400 |
| Unmanaged directory-view observations | 400 |

Every lifecycle passed exact bytes, modes, ownership, lookup and readdir
membership, stable-root `openat()`, old-descriptor behavior, no-op identity,
rollback, policy-counter conservation, direct selected-object identity, lower
preservation, and cleanup. All 20 boots had zero mechanism status, clean
declared dmesg checks, empty external BPF/FUSE inventory, successful ext4
unmount and removal, removed host block images, and verified vCPU affinity.

## Primary Result

The ratio is the boot-clustered paired `namei_ext / AtomicWriter` lifecycle
ratio. Confidence intervals resample the 20 independent boot-level medians.

| Union paths | AtomicWriter median | `namei_ext` median | Paired ratio, 95% CI |
| ---: | ---: | ---: | ---: |
| 4 | 6.122 ms | 10.655 ms | 1.742 [1.725, 1.771] |
| 16 | 6.891 ms | 11.015 ms | 1.614 [1.565, 1.651] |
| 64 | 10.387 ms | 12.743 ms | 1.237 [1.220, 1.269] |
| 256 | 33.708 ms | 23.472 ms | **0.720 [0.680, 0.731]** |

At the predeclared 256-path endpoint, `namei_ext` has 28.0% lower paired
per-volume lifecycle time after the policy has been loaded and attached. The
result is a crossover, not a universal performance advantage: fixed setup cost
dominates at 4, 16, and 64 paths.

At 256 paths, the median phase decomposition is:

| Phase | AtomicWriter | `namei_ext` |
| --- | ---: | ---: |
| Per-volume setup | 0.181 ms | 17.141 ms |
| Four publications | 25.517 ms | 0.004 ms |
| Four consumer observations | 6.615 ms | 6.325 ms |
| Separately measured BPF load/attach | N/A | 12.937 ms |

The `namei_ext` setup consists of 5.820 ms object preparation, 10.786 ms
target registration, 0.471 ms map population, and 0.017 ms consumer cgroup
movement. The consumer times are similar; the 256-path lifecycle advantage
comes from selecting already-prepared generations instead of materializing a
new timestamp tree for each changed publication.

## Filesystem Work

At 256 paths, AtomicWriter created 765 payload files and wrote 8,413 payload
bytes over initial publication, update, and rollback. `namei_ext` prepared 510
lower-generation files, 256 empty logical placeholders, and 5,608 payload
bytes. Thus this run supports 33.3% fewer payload bytes for the measured
two-generation lifecycle, but not fewer regular filesystem objects:
`namei_ext` prepared 766 regular files versus AtomicWriter's 765 newly
materialized payload files.

## Measurement Boundary And Deviation

The main result is not a cold-start result. The runner loads and attaches one
policy at the cgroup root before the per-volume timer; the policy maps then key
decisions by child cgroup ID. Repeating the measured 12.937 ms load/attach for
every volume would yield a diagnostic 256-path ratio of 1.099
[1.039, 1.136], so the result must always be described as a per-volume
lifecycle with a preloaded policy. It must not be called unqualified
end-to-end or cold-start performance.

After the third real preflight, target registration was changed from one
helper per target to one helper and repeated control writes. The changed
registration path passed an independent modified-kernel KVM functional test,
AB/BA equivalence checks, host correctness tests, and claim-to-code review,
but the complete quantitative preflight was not repeated before the formal
matrix. This is a procedural deviation from the normal preflight ordering. It
does not alter the frozen workload, source baseline, primary metric, matrix,
or oracle, and the complete 20-boot formal run exercised the changed path
without failure.

## Independent Result Review

- **Run status:** valid.
- **Tested hypothesis:** supported at the predeclared 256-path endpoint with a
  preloaded and attached policy.
- **Research value:** supporting.
- **Paper impact:** additional RQ1 evidence and a measured workload boundary.
- **Next paper decision:** add the scoped four-point scaling result and
  decomposition. Do not claim universal lower cost, cold-start advantage,
  fewer filesystem objects, a running-kubelet comparison, or complete
  ConfigMap/service behavior.

The result remains limited to the two-known-generation, already-materialized
payload-view subset. ConfigMap retrieval, unseen-generation arrival, kubelet
reconciliation, symlink and inotify behavior, validation, service reload, and
reload failure handling remain outside the experiment.

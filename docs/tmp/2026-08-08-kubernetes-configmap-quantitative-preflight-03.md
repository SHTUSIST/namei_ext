# Kubernetes ConfigMap Quantitative Preflight 03

## Outcome

The third bounded W4 preflight completed the entire source and `namei_ext`
workload matrix on the modified kernel, then the owning Make target failed on
two incorrect finalizer predicates. The immutable result remains `failed` and
is not a paper performance result. It must not be rerun, finalized, analyzed in
place, or relabeled as successful.

- Command: `make kvm-kubernetes-configmap-quantitative-preflight
  RUN_ID=20260808T133403Z-w4-quantitative-preflight03`
- Result root:
  `results/experiments/kubernetes-configmap-quantitative-preflight/20260808T133403Z-w4-quantitative-preflight03`
- Source commit: `0b2f127a92ab14da8a961627b257bd56dd311cb2`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Guest release: `7.1.0-rc7-gb07117a3cb41`
- Frozen matrix: one boot, two pairs, widths 16 and 256, AB/BA order.

## Completed Raw Evidence

The boot produced 3,816 JSONL rows before cleanup:

| Event | Rows | Failed rows |
| --- | ---: | ---: |
| lifecycle | 8 | 0 |
| selected-object identity | 2,720 | 0 |
| lower-object preservation | 1,080 | 0 |
| unmanaged directory | 4 | 0 |
| AtomicWriter materialization audit | 4 | 0 |

The captured validator independently recomputed this boot-local JSONL against
the frozen `run.json` and passed. All eight lifecycle rows passed the exact
bytes, modes, ownership, directory membership, persistent-descriptor,
no-op/rollback identity, lower-preservation, policy-counter, and cleanup
oracles. This covers both 16-entry and maximum-policy-width 256-entry cases.

The runtime substrate also completed as designed:

- QMP readback showed four vCPUs pinned one-to-one to host CPUs 4--7.
- The guest found the 1 GiB `namei_ext_w4` virtio disk as `/dev/vda`.
- `blkid`, `statfs`, and `findmnt` recorded ext4 with
  `rw,nosuid,nodev,noatime`.
- Guest dmesg and pre/post external BPF/FUSE inventories passed.
- Host raw-image removal returned zero and the image is absent.

## False Negatives

`mechanism.status` was written before unmount and had value 2. The exact failing
command was the mechanism's last command: it required the mounted sample root to
contain no entries. A newly formatted ext4 filesystem contains `lost+found`, so
this predicate rejected the expected filesystem state after every workload
directory had been removed. The repaired predicate requires `lost+found` to be
an empty directory and rejects every other top-level entry.

The outer guest cleanup then recorded:

```text
unmount_status=0
mount_lookup_status=1
mountpoint_status=32
root_remove_status=0
root_absent=true
```

The harness incorrectly required `mountpoint_status=1`. util-linux
`mountpoint -q PATH` returns 32 when `PATH` exists but is not a mountpoint. The
command therefore diagnosed exactly the desired post-unmount state, but the
summary comparison independently set `cleanup_status=1`. The owning target
correctly froze `run.json` as failed only after guest and host cleanup evidence
was written.

## Repair And Gate

The forward fix recognizes only an empty ext4 `lost+found` at mechanism
completion, requires 32 in both guest cleanup and finalization, and adds host
regression checks for both predicates. No workload, baseline, timing boundary,
sample count, filesystem, CPU control, policy, or source oracle changes.

Because this was the third bounded preflight, there is no fourth preflight.
The independent audit confirmed that all 3,816 workload rows and resource
cleanup completed, but formal execution remains **NO-GO**. Diagnostic timing in
this failed root showed that `namei_ext` setup dominates its lifecycle time. The
current harness forks, migrates to the cgroup, and waits once per target, making
that process pattern the first candidate to measure rather than an established
cost attribution. The next step is to measure setup phases separately and then
evaluate batching registration in one control process without changing the
primary lifecycle timer. Functional gates and a focused independent review must
pass before the frozen 20-boot run.

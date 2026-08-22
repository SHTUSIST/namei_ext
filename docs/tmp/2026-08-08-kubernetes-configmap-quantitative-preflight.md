# W4 Quantitative KVM Preflight

## Decision

The second invocation completed the real modified-kernel path and is a
**correctness GO but performance-protocol NO-GO**. The lifecycle and cleanup
evidence is valid. Its timings are not admissible as paper results because the
timed sample parents were created under the host-shared result directory and
therefore exercised the KVM 9p filesystem. The frozen root remains useful as a
correctness preflight and must not be rerun or reanalyzed in place.

## Invocation And Identity

- Command: `make kvm-kubernetes-configmap-quantitative-preflight
  RUN_ID=20260808T125047Z-w4-quantitative-preflight02`
- Result root:
  `results/experiments/kubernetes-configmap-quantitative-preflight/20260808T125047Z-w4-quantitative-preflight02`
- Source commit: `8ea984f3448668cb1e47bcdae3029da3bc927175`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Guest release: `7.1.0-rc7-gb07117a3cb41`
- Kubernetes source: v1.30.0 commit
  `7c48c2bd72b9bf5c44d21d7338cc7bea77d0ad2a`
- Matrix: one boot, width 16, two paired lifecycles, counterbalanced AB/BA
  order, followed by two separate source materialization audits.

Both source trees were clean at capture. The run, boot, captured validator, and
captured analyzer completed. The boot recorded no BPF program, cgroup
attachment, FUSE mount, or FUSE open descriptor before or after the suite. All
workload stderr logs and the launcher stderr log are empty. The configured
dmesg rejection gate passed.

## Correctness Evidence

The frozen JSONL has 228 rows, all accepted by the captured validator:

- four lifecycle rows: two official Kubernetes `AtomicWriter` and two real
  `namei_ext`, all with `pass=true` and `cleanup_pass=true`;
- 160 selected/unmanaged identity rows;
- 60 direct lower-object preservation rows;
- two exact unmanaged-directory rows; and
- two official-source materialization-audit rows.

Every lifecycle observed `initial -> update -> no-op -> rollback` through the
same persistent uid/gid 1000 consumer. Per state, the consumer performed three
directory enumerations, 15 reads, 17 stats, one expected missing open, 16 opens
for the initial state and 15 thereafter, plus one retained-old-FD read after
the initial state. Exact bytes, modes, owners, sizes, visible names, and object
identities passed. The old V0 descriptor retained `version=0\n`; the update and
no-op selected the same V1 object; `namei_ext` rollback selected the original
V0 object, while `AtomicWriter` rollback materialized a new V0 object.

Each `namei_ext` pair recorded 945 policy decisions: 849 lookup and 96 readdir
events, comprising 120 selects, 813 passes, and 12 hides. Both pairs preserved
30 lower files and 328 bytes with unchanged device, inode, type, bytes, mode,
owner, size, mtime, and ctime. They also passed 60 managed identity checks, four
managed hidden checks, 16 unmanaged-scope checks, empty view maps, policy and
target teardown, cgroup removal, lower/logical removal, and zero residual BPF
inventory.

Each independent `AtomicWriter` audit directly observed 15 live payload files
at every state. Initial, update, and rollback created 15 payload files each;
the no-op created none and retained the update `..data` target. Thus each audit
observed 45 newly materialized regular files and 493 payload bytes across the
three changing publications. Both audit roots were removed completely.

## Timing Sanity Only

The captured analyzer deliberately returned `preflight-complete` with no
confidence interval. At width 16 it reported median begin-to-end times of
2970.647 ms for `AtomicWriter` and 1531.724 ms for `namei_ext`, a ratio of
0.509. Individual `namei_ext` attach times were 19.608 and 21.692 ms. These
values demonstrate positive monotonic timing fields and phase decomposition;
they are not paper performance evidence.

The sample parent used by both mechanisms was below the host-shared result
root. The approximately one-to-three-second lifecycle times for 16 tiny files
are materially affected by this 9p substrate, but this run cannot quantify how
much of either mechanism's time 9p contributes. Counterbalancing does not make
that substrate representative of a local filesystem, and the favorable ratio
must not enter `docs/evaluation.md`, a figure, the paper, or a proposal.

## Required Fix Before Formal Run

The next harness revision must attach one fresh host-ext4-backed raw virtio
block device per boot, format and mount ext4 in the guest, and place both
mechanisms' sample parents there. Ordinary ConfigMap volume uses a non-memory
`EmptyDir`; using tmpfs or a memory-backed loop image would instead match
projected volume and change the source workload. The harness must record and
validate host backing, guest block, ext4 mount, `blkid`, and `statfs` identity;
keep result serialization outside the primary timer; separately gate guest
unmount/mountpoint cleanup and host image removal; and retain the same AB/BA
matrix and correctness oracle. The analyzer must also expose the frozen phase,
attach, per-state live-object, and materialization metrics rather than only the
primary ratio.

After host tests and independent code review, one final KVM preflight may test
that corrected protocol in a new immutable result root. The 20-boot formal run
remains **NO-GO** until that preflight and its raw-evidence review pass.

# W4 Quantitative Harness Implementation

## Motivation

The completed W4 case establishes the Kubernetes-derived four-state view on the
modified kernel but does not measure the work needed to create, publish,
consume, and roll back that view. This implementation realizes the frozen plan
in `2026-08-07-kubernetes-configmap-quantitative-plan.md`. It compares the
official Kubernetes v1.30.0 `AtomicWriter` implementation with the real
`cgroup/namei_ext` path for the same two-known-generation lifecycle. It is a
source-relative RQ1 extension, not the paper's FUSE comparison.

## Implemented Paths

- `experiments/kubernetes_configmap_quantitative/atomic_writer_quantitative_driver.go`
  imports `k8s.io/kubernetes/pkg/volume/util` from the pinned v1.30.0 vendor
  tree and invokes `AtomicWriter.Write()` for V0, V1, repeated V1, and V0
  rollback. Only after the entire boot's timed AB/BA matrix completes, separate
  fresh audit roots observe every live timestamp payload before the next audit
  write can remove it. No audit write or walk precedes a timed condition.
- `experiments/kubernetes_configmap_quantitative/namei_ext_configmap_quantitative.c`
  loads and attaches the existing ConfigMap policy, creates a per-sample
  cgroup, prepares both lower generations and logical placeholders, registers
  target paths, populates both view maps, and performs the same 0/1/1/0 state
  sequence.
- `experiments/kubernetes_configmap_quantitative/configmap_consumer.c` is the
  common persistent non-root consumer. At every state it enumerates all three
  directories; opens, reads, and stats every visible leaf; checks the hidden
  leaf; retains the initial root and app descriptors; and returns a fixed-size
  timed acknowledgement. After rollback it exports the bytes, modes, owners,
  sizes, object identities, and directory names retained in memory.
- `analysis/kubernetes_configmap_quantitative/validate.py` independently
  reconstructs the expected payload and rejects any mismatch in the lifecycle
  matrix, consumer operations, bytes, modes, ownership, directory membership,
  selected-object identity, unmanaged placeholders, lower-object contents or
  metadata, state order, materialization observations, or cleanup status.
- `analysis/kubernetes_configmap_quantitative/analyze.py` computes paired
  complete-lifecycle ratios, boot-level median log-ratios, and formal
  bootstrapped intervals without interpreting a one-boot preflight as a
  performance claim.
- `mk/experiments/kubernetes_configmap_quantitative.mk` owns build, source,
  host tests, immutable result creation, guest execution, final validation,
  and analysis. The corrected preflight is one boot with two pairs at widths 16
  and 256 in AB/BA order. The frozen formal matrix is 20 boots, five pairs per
  scale, and widths 4, 16, 64, and 256.

## Timing Boundary

Both conditions begin with a persistent consumer and one empty, non-root-owned
sample parent. The complete lifecycle starts before any condition-specific
root, file, cgroup, target registration, or map update. It ends after the
consumer acknowledges rollback. That begin-to-end wall span is the primary
metric. The measured active-phase sum includes each
mechanism's setup, four publications, and the identical consumer sequence. The
one-time BPF load and attach is measured separately. Full JSON serialization,
the separate AtomicWriter audit lifecycle, exhaustive evidence capture, lower
replay, unmanaged-view validation, teardown, and dmesg checks are outside the
timer and remain mandatory gates.

## Raw Evidence

Every lifecycle row preserves the four consumer acknowledgements instead of a
summary boolean. Each acknowledgement contains the exact file set, bytes,
mode, owner, size, device and inode, exact directory entries, operation counts,
and persistent-descriptor identities. The `namei_ext` condition additionally
emits one row per managed or unmanaged pathname observation, one row per lower
file and generation with before/after contents and metadata including mtime and
ctime, and one exact unmanaged-directory inventory. The source audit emits a
separate materialization event after each boot's timing matrix. The
captured validator is run from the result root before analysis.

No checksum manifest or checksum gate is added. The evidence is semantic and
can be recomputed directly from the raw JSONL.

## Host Validation

The following Make-owned gates pass:

- `make kubernetes-configmap-quantitative` builds both C programs with
  `-Wall -Wextra -Werror`.
- `make kubernetes-configmap-quantitative-source` builds the Go driver against
  the pinned official Kubernetes vendor tree.
- `make kubernetes-configmap-quantitative-host-test` completes all four
  `AtomicWriter` states at width 16 with the common non-root consumer and leaves
  an empty sample parent.
- `make kubernetes-configmap-quantitative-host-failure-test` verifies that a
  consumer failure produces a failed structured lifecycle and still cleans the
  sample root.
- `make kubernetes-configmap-quantitative-analysis-test` passes 15 tests,
  including rejection of altered consumer bytes and lower-file timestamps and
  a regression test that distinguishes the begin-to-end wall span from the
  active-phase sum.
- Python compilation, `make -qp`, and `git diff --check` pass.

## Review And Remaining Gate

The first implementation review rejected a guest-root/source-nonroot mismatch,
insufficient lower/unmanaged evidence, and incomplete source failure cleanup.
Those issues were fixed by deriving the runtime owner from each sample parent,
using the official `setPerms` callback, preserving full lower metadata, replaying
the selected generations after timing, validating the unmanaged namespace, and
always writing and cleaning a failed source observation. A second review
required raw consumer contents and directory names plus lower contents, type,
mtime, and ctime so the source oracle could be independently recomputed. Those
fields and a rejecting validator are now implemented.

The final claim-to-code review initially returned no-go because full consumer
evidence was serialized inside the timer, source audits could perturb later
timed samples, the analyzer treated an active-phase sum as the primary metric,
and matrix, cleanup, metadata, and result-root checks were too weak. The timed
consumer now emits only a bounded acknowledgement and exports full evidence
after rollback; every timed AB/BA condition completes before any separate
source audit; the primary metric is the begin-to-end wall span; and the
validator requires exact matrix, phase-sum, audit, identity, and cleanup
evidence. Immutable execution, finalization, and analysis markers prevent a
failed or interrupted result root from being reused. An independent reread
returned **GO** for exactly one modified-kernel KVM preflight.

At implementation admission, no KVM result existed for this harness. The first
real modified-kernel result and its later review are recorded separately below.

## Post-Preflight Protocol Correction

The completed correctness preflight exposed that sample parents were below the
host-shared result directory. The measured file operations therefore used 9p,
making the timing sanity values ineligible for the paper. The source path was
rechecked before choosing a replacement: ordinary ConfigMap volume uses a
non-memory `EmptyDir`, whereas projected volume requests memory-backed storage.
The corrected implementation creates a fresh raw image on the host's ext4
filesystem for every boot, attaches it directly as a `cache=none` virtio block
device, and has the guest format and mount ext4. It records host backing, guest
device, `findmnt`, `blkid`, and `statfs` identity, runs both mechanisms below
that common mount, and separately gates guest unmount/mountpoint cleanup and
host image removal. Result serialization remains outside the primary timer.
The next immutable preflight must validate this correction before the formal
matrix is admitted.

# Kubernetes ConfigMap Quantitative Protocol Repair

## Motivation

The first completed modified-kernel preflight passed its source and `namei_ext`
correctness oracles, but both mechanisms created timed sample trees below the
host-shared result root. Those file operations therefore traversed 9p. The
result is retained as immutable correctness evidence, but none of its timings
are admissible as local-filesystem performance evidence.

## Source And Substrate Decision

The Kubernetes v1.30.0 source was reread before changing the protocol. Ordinary
ConfigMap volume setup creates a non-memory `EmptyDir` in
`pkg/volume/configmap/configmap.go:178`; projected volume setup requests
memory-backed storage in `pkg/volume/projected/projected.go:61`. W4 models
ConfigMap publication through the official `AtomicWriter`, so the corrected
experiment uses an ext4 filesystem on a host-disk-backed virtual block device
rather than silently changing the source workload to projected volume on
tmpfs.

An independent admission review rejected a loop image under virtme's `/tmp`:
although the visible filesystem was ext4, `/tmp` is an overlay whose upper is
memory-backed, so this was still not the source-matched substrate. Each boot now
creates a fresh 1 GiB raw image on the host's `/dev/nvme0n1p2` ext4 filesystem,
attaches it directly as a `cache=none` virtio block device, and has the guest
identify it by serial, format ext4, and mount with `noatime,nosuid,nodev`. Every
timed `AtomicWriter` and `namei_ext` sample is below that mount. Raw observations
and logs remain in the result root and are written outside the primary timer.
Host backing, guest block identity, `findmnt`, `blkid`, and `statfs` are retained.
Guest unmount, mount lookup, mountpoint removal, and host image removal are
recorded and gated separately rather than collapsed into one cleanup status.
Lazy inode-table and journal initialization are disabled and the mounted
filesystem is synchronized before any condition starts, so ext4 initialization
cannot run asynchronously inside one timed condition.

The KVM capture helper also supports deferred failure commitment for this
external host resource. A launch or guest failure first records its cause; the
caller then removes the raw image and writes host cleanup evidence, and only
after those writes changes `run.json` to `failed`. Successful runs have the same
path as before. This keeps both completed and failed result roots immutable once
their terminal status is installed.

## Host Execution Controls

The experiment freezes four KVM vCPUs, 8 GiB memory, and a one-to-one mapping to
host CPUs 4--7. The QMP pinning helper reads every vCPU affinity back; the guest
waits for this verified record before starting the workload. Finalization
requires the same mapping and the guest-side barrier value. The result also
records CPU topology and frequency policy, selected CPUs, `vng` version, and
host `/proc/stat` and `/proc/interrupts` before and after the run.

## Oracle And Analysis Repair

The validator now checks object identity for every selected file across no-op
and for every persistent V0 lower object across `namei_ext` rollback. The
source baseline's audit remains the authoritative all-file evidence that
rollback created a new generation: unlinked source files may legally receive
reused inode numbers, while the persistently open `config/app.conf` provides a
separate retained-object identity check. The validator also recomputes the two
BPF counter conservation equations and rejects a `run.json` that does not
contain the frozen launch and ext4 protocols.

The analyzer still treats begin-to-end lifecycle time at width 256 as the
primary metric. It additionally reports publication-only and consumer-only
cost, all setup/publication/consumer phases, BPF attach time, source
materialization work and per-state live objects, and the lower, logical, and
per-state visible objects prepared by `namei_ext`. It writes scaling,
decomposition, and materialization CSV files in addition to raw-preserving JSON
and Markdown output. A one-boot preflight still produces no confidence or
superiority claim; boot-clustered confidence intervals are enabled only for the
frozen 20-boot experiment.

## Validation And Next Gate

The 15 analysis and validator tests cover the new protocol rejection, object
identity, counter conservation, secondary metrics, and output artifacts. The
complete host build, source, success, failure, analysis, parse, and diff gates
pass for this repair. A `vng --dry-run` expansion additionally showed one final
QEMU command containing both QMP and an explicit raw `cache=none`
`virtio-blk-pci` device with serial `namei_ext_w4`; it did not start KVM or
create a result. An independent code review must return GO for one final
immutable preflight containing widths 16 and 256. Only a successful raw-evidence
review of that result can admit the 20-boot formal experiment.

The second admission review found no P0 and confirmed the QEMU wiring, required
kernel configuration, guest device discovery, timing path, analyzer outputs,
and absence of new checksum gates. Its three local P1 findings are addressed in
the current revision: block-create and host-backing failures use the same
raw-status cleanup and install the terminal failed state only afterward, and
finalization parses the captured `findmnt` options to require `noatime`,
`nosuid`, and `nodev` rather than trusting `run.json` alone.

A final focused reread found no remaining issue in those three paths. It
confirmed that create/backing, KVM/guest, and host-cleanup failures all retain
their raw status before the terminal failed state; the actual ext4 options are
validated; and the memory-backed loop-image defect is absent. Its verdict is
**GO** for this commit and **GO** for exactly one corrected KVM preflight.

That preflight exposed two finalizer-oracle errors after all workload rows had
completed. First, the mechanism required the root of a newly formatted ext4
filesystem to be empty, although `mkfs.ext4` creates `lost+found`. Second,
util-linux `mountpoint -q` returns 32, not 1, when an existing path is not a
mountpoint. The mechanism now requires an empty `lost+found` and no other
top-level entries. Guest cleanup and finalization require return 32, and the
host success test protects both predicates. The raw evidence showed all workload
oracles passing, successful unmount, an absent exact mount in `findmnt`, return
32 from `mountpoint`, successful mountpoint-directory removal, and an absent
host image. The failed immutable run and its audit are recorded separately; it
is not reused as performance evidence.

The audit also identified an avoidable candidate bottleneck within setup: every
registered target currently forks a process, moves it into the policy cgroup,
and polls for completion. Aggregate timing proves only that setup dominates;
the failed run's timing is diagnostic only, but it is enough to reject immediate
formal execution. Separate setup-phase measurements must first establish the
cost attribution, after which target-registration batching can be evaluated.
Both changes must pass functional review before the frozen 20-boot experiment
begins.

A final independent reread found no remaining commit blocker after the host
test gained negative cases for a nonempty `lost+found` and an extra top-level
entry, and after the setup diagnosis was limited to the evidence actually
measured. Its verdict is **GO** for this forward-fix commit. It did not rerun KVM
or modify the immutable failed result.

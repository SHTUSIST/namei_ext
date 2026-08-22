# Spindle RQ2 FUSE Invalidation-Order Repair

## Motivation

Formal02 established a common tmpfs lower filesystem but stopped in its first
FUSE boot. With the affected logical object held open using `O_PATH`, FUSE inode
invalidation returned zero and the following entry invalidation returned
`-ENOENT` for both permission transitions. The strict gate correctly prevented
the incomplete comparison from continuing.

This repair tests only whether notification order caused that failure. It does
not change the Spindle workload, feature-equivalent behavior, correctness
oracle, cache timeouts, passthrough mode, samples, repetitions, primary metric,
or accepted statuses.

## Code Paths Inspected

- `experiments/spindle_staging/namei_ext_spindle_staging_rq2.c`: parent-side
  `O_PATH` pin, control request, FUSE notification calls, and zero-status gates;
- `kernel/fs/fuse/dir.c:fuse_reverse_inval_entry()`: parent lookup, alias lookup,
  positive-entry check, dentry invalidation, and entry-cache invalidation;
- `kernel/fs/fuse/inode.c:fuse_reverse_inval_inode()`: selected-inode lookup,
  attribute invalidation, ACL invalidation, and page-cache invalidation;
- libfuse 3.18.2 `fuse_lowlevel_notify_inval_entry()` and
  `fuse_lowlevel_notify_inval_inode()`: notification construction and return
  contract.

## Change

The daemon now sends `FUSE_NOTIFY_INVAL_ENTRY` before
`FUSE_NOTIFY_INVAL_INODE`. It still holds userspace references to both FUSE
inode records, while the requester still holds the selected logical object with
`O_PATH` until both replies arrive. The response retains separate entry and
inode statuses and fails if either is nonzero.

The overall return now reports an entry error before an inode error, matching
execution order. Raw events and the analyzer continue to require all three
permission/removal notifications and both component statuses to equal zero.

## Host Validation

`make spindle-staging-rq2-host-gate` passed. The target rebuilt the runner with
`-Werror`, verified static libfuse 3.18.2, retained kernel FUSE passthrough, and
passed all eight analyzer regression tests. The analyzer still rejects a
non-tmpfs lower filesystem, any nonzero entry-notification status, failed
oracles, missing samples, duplicate samples, and invalid mechanism windows.

Host validation cannot exercise reverse invalidation in the modified kernel.
The next KVM run remains the decisive check.

## KVM Validation Plan

Run the unchanged complete paired matrix in a fresh result root. The first FUSE
boot is the real test of the ordering hypothesis. If either notification still
returns nonzero, retain that root as a failure and inspect the exact FUSE
parent/name cache state instead of relaxing the gate.

## KVM Outcome

Formal03 falsified the ordering hypothesis: entry-first still returned
`-ENOENT`, while the following inode notification returned zero. The root was
retained and the matrix stopped. Subsequent primary-source inspection showed
that notification status alone cannot establish the pathname state. The
experiment plan now admits an absent entry only when a direct non-root pathname
probe and the application-level state-transition oracle both pass.

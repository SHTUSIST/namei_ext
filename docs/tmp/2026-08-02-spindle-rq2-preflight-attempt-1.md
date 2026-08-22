# Spindle RQ2 KVM preflight attempt 1

## Scope

This record diagnoses the first real two-boot KVM preflight for the W6 Spindle
staging RQ2 comparison. The immutable failed result root is:

```text
results/experiments/spindle-staging-rq2-preflight/20260802T134059Z-w6-rq2-preflight02/
```

An earlier invocation stopped before result-root creation because host CPUs
4--7 had returned to the `powersave` governor. No KVM was launched and no
workload observation was produced by that invocation. The four pinned CPUs
were restored to `performance` without changing the workload or protocol.

## Observed result

The namei_ext boot completed one warmup and five measured source-derived
Spindle launches. In the FUSE boot, all 47 identity checks, the warmup, and all
five measured launches also passed. Cleanup, BPF/FUSE inventory, cgroup
cleanup, and dmesg gates passed. The FUSE condition then failed before running
the non-privileged permission probe, so the complete run correctly remained a
failed preflight and supports no performance claim.

The decisive raw rows are:

```text
mode_zero:   status=-2 inode_status=0 entry_status=-2 pass=false
mode_restore: status=-2 inode_status=0 entry_status=-2 pass=false
permission: observed_errno=0 pass=false
```

`observed_errno=0` did not mean that FUSE allowed the open. The runner executed
the permission child only when the invalidation request returned zero; the
`-ENOENT` return therefore skipped the probe and left its initialized value
unchanged.

## Root cause and repair

`fuse_lowlevel_notify_inval_entry()` returns the kernel result of
`fuse_reverse_inval_entry()`. The modified kernel's `fs/fuse/dir.c` returns
`-ENOENT` when the parent alias or named dentry is not in the dcache. In this
run, the target inode invalidation succeeded and the entry invalidation returned
`-ENOENT`, meaning there was no cached entry to remove. Treating that state as
an invalidation failure was a runner-oracle defect.

The runner now preserves `entry_status=-ENOENT` in the raw event but accepts it
when inode invalidation succeeded. The permission child will therefore execute
and must still observe `EACCES` after the selected lower object's mode changes
to zero. Any other inode or entry invalidation error remains a hard failure.

## Next gate

The failed root is not modified or reused. After strict host validation,
independent review, commit, and push, the repair requires a fresh two-boot KVM
preflight with a new run ID. W1--W7 remain unchanged; this repair only adds W6
RQ2 depth.

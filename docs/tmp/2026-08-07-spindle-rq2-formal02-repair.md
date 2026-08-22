# Spindle RQ2 Formal02 Repair

## Motivation

Formal01 completed its 20-boot matrix but did not isolate policy-placement cost.
The runtime tree remained on virtme's uncached 9p workspace while the FUSE arm
added a cached passthrough view over that tree. It also accepted
`FUSE_NOTIFY_INVAL_ENTRY=-ENOENT` although the approved protocol required both
entry and inode notifications to succeed.

Formal02 keeps the workload, conditions, primary metric, repetition count,
correctness oracle, and FUSE configuration unchanged. It repairs the execution
layout and makes the existing gates independently enforceable.

## Code Paths Inspected

- `mk/experiments/spindle_staging_rq2.mk`: per-boot runtime construction,
  modified-kernel launch, guest cleanup, and analyzer invocation;
- `experiments/spindle_staging/namei_ext_spindle_staging_rq2.c`: FUSE control
  requests, inode/entry notification, condition setup and teardown, measured
  process resources, and raw event emission;
- `kernel/fs/fuse/dir.c` and `kernel/fs/fuse/inode.c`: reverse entry and inode
  invalidation behavior;
- libfuse 3.18.2 `fuse_lowlevel.h`: notification return contracts;
- `analysis/spindle_staging_rq2/analyze.py`: raw validity checks, paired
  aggregation, and report generation.

## Implementation

### Common Lower Filesystem

Each guest now mounts a 512 MiB tmpfs at the compiled Spindle runtime root and
copies the pinned 160 MiB runtime tree into it before source population. Both
conditions therefore execute the same test driver and non-policy dependencies
from guest-local tmpfs. The guest records the resolved runtime filesystem type
and fails unless it is exactly `tmpfs`. The analyzer independently requires
one matching lower-filesystem event for every condition and repetition.

The tmpfs mount uses the existing compiled-root mountpoint and is removed by
the existing recursive guest cleanup. The source archive, test driver, Spindle
commit, 47 mappings, logical paths, argv, environment, uid/gid, cache tmpfs,
and boot resources are unchanged.

### Strict FUSE Invalidation

Before the parent sends an invalidate or withdrawal control request, it opens
the affected logical target with `O_PATH` and holds that descriptor until the
daemon replies. This pins the positive dentry while the daemon sends both
notifications. The runner no longer translates `-ENOENT` into success and now
requires the overall response, inode status, and entry status all to be zero
for mode removal, mode restoration, and withdrawal.

The descriptor is outside the measured window. `O_PATH` does not add a regular
FUSE open or data-path access, and it is closed after the notification reply.
The independent permission and withdrawn-loader oracles remain mandatory.

### Planned Explanatory Metrics

The runner now records per-condition setup and teardown duration. The analyzer
independently checks the lower filesystem and exact invalidation statuses,
validates the measured process resource fields, and emits:

- per-condition pooled p50 and p95 loader latency;
- client CPU, major/minor faults, and FUSE daemon CPU;
- paired FUSE/namei_ext total-CPU ratio with a bootstrap interval;
- median setup and teardown duration;
- `namei_ext` selections and FUSE callbacks per measured launch.

The primary paired median-latency ratio and its interpretation are unchanged.

## Validation

`make spindle-staging-rq2-host-gate` passed after the repair. It rebuilt the
runner with `-Werror`, confirmed static libfuse 3.18.2 and kernel passthrough
support, and passed eight analyzer tests. The new tests prove that a non-tmpfs
runtime or a nonzero FUSE entry-invalidation status is rejected before timing
interpretation.

After an independent implementation review found that the lower-filesystem
event initially repeated a literal and mechanism-window keys were not range
checked, the raw event was changed to read the mandatory `findmnt` output and
the analyzer was changed to require exactly repetitions 1 through N. A
regression test covers both mechanism-window event types. The final host gate
passes all eight analyzer tests, and the same reviewer returned GO after the
follow-up.

Host validation cannot prove the modified-kernel mount and notification path.
The next step is a fresh full formal matrix. Any failure in its first affected
boot must terminate the run; the failed root must not be repaired or reused.

## Subsequent Correction

Formal02 and formal03 both stopped because entry notification returned
`-ENOENT`; changing notification order did not alter it. Inspection of pinned
kernel and libfuse paths then showed that `-ENOENT` can mean that no parent or
positive parent/name kernel entry was found. The amended protocol preserves
that status, accepts only zero or `-ENOENT` for entry invalidation, still
requires inode invalidation to return zero, and requires a non-root `fstatat`
`ENOENT` result plus the independent loader oracle to reject stale behavior.
The original exact-zero claim in this historical repair is superseded by that
amendment.

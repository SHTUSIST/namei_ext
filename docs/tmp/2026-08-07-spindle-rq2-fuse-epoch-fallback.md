# Spindle RQ2 FUSE Epoch Fallback

## Motivation

Preflight05 showed that all three per-entry FUSE invalidations returned
`-ENOENT`, while the post-withdrawal pathname remained visible from the kernel
cache. The selected-backing open counter did not increase, confirming that the
following `fstatat` did not reach a fresh FUSE lookup. Accepting the notification
status without repairing the cache would make the FUSE arm semantically weaker
than the namei_ext arm.

## Mainline Mechanism

libfuse 3.18.2 exposes `fuse_lowlevel_notify_increment_epoch()`. FUSE protocol
7.44 defines `FUSE_NOTIFY_INC_EPOCH`. In the pinned kernel,
`fuse_notify_inc_epoch()` increments the connection epoch; FUSE dentry
revalidation rejects entries from an older epoch and performs a new lookup.
This is an upstream cache-invalidation mechanism, not project-side emulation.

## Implementation

The baseline still attempts targeted parent/name invalidation first. If and
only if that call returns `-ENOENT`, the daemon issues the connection-epoch
notification before returning control to the workload. Inode invalidation is
retained for attribute changes. Every raw invalidation row now includes:

- `entry_status` and `inode_status`;
- `epoch_attempted`;
- `epoch_status`;
- the combined operation status and pass result.

The analyzer and per-boot Make gate require three valid notification rows per
FUSE boot. Entry status zero requires no epoch fallback. Entry status
`-ENOENT` requires an attempted fallback with status zero. The direct pathname,
loader-diagnostic, and backing-engagement oracles remain mandatory after the
notification, so a successful fallback return alone receives no correctness
credit.

## Validation

The statically linked RQ2 runner compiles with `-Werror`. Fifteen analyzer tests
pass, including rejection of a missing fallback, a failed fallback, and a
stale withdrawn pathname. `make spindle-staging-rq2-host-gate` passes. One final
fresh paired KVM preflight remained required before the formal matrix.
Preflight06 subsequently passed all three epoch fallbacks and the following
application-level pathname oracles.

## Scope

The fallback occurs only during the correctness transitions outside the warm
loader timing window. It does not change the FUSE daemon configuration,
passthrough data path, workload, sample count, or primary performance metric.

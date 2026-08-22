# Spindle RQ2 Preflight Attempt 3

## Purpose

This was the third real paired KVM preflight for the approved W6 Spindle RQ2
comparison. It tested the current `namei_ext` condition and the optimized
libfuse 3.18.2 condition over the same 47 source-produced Spindle cache
objects. Each condition used a fresh modified-kernel boot, one warmup, and five
measured loader launches.

The result root is:

`results/experiments/spindle-staging-rq2-preflight/20260808T062934Z-w6-rq2-preflight05/`

## Observed Result

The `namei_ext` boot completed. The FUSE boot reached and passed source
population, all 47 logical-to-cache identity checks, the permission transition,
one warmup, five measured launches, all 47 per-target engagement checks, 400
passthrough opens, zero userspace read fallback, and cleanup. Its measured
window recorded 1 user tick, 1 system tick, 21,631,941 ns of scheduler runtime,
1,620 voluntary context switches, and a daemon thread count that changed from
6 to 7.

The runner then failed the FUSE condition before the withdrawal check. The only
failed raw event was `spindle-staging-rq2-fuse-resource`: the implementation
required the daemon thread count to remain exactly constant. Libfuse's
multithreaded loop can create workers lazily, so 6 to 7 is a valid daemon state
transition. The approved plan lists daemon thread and CPU observations as
secondary explanatory measurements; constant thread count is not a
correctness, fairness, or mechanism-engagement oracle.

The top-level result is therefore a failed preflight and is not performance
evidence. The result root remains unchanged.

## Repair

`rq2_daemon_resource_monotonic()` now requires only a positive daemon thread
count at both snapshots. It continues to require monotonic cumulative CPU,
scheduler, and context-switch counters, and the FUSE condition still requires
positive scheduler runtime during the measured window. The raw record still
contains both thread counts. No workload, baseline, timing metric, source
oracle, permission oracle, withdrawal oracle, or engagement requirement was
weakened.

## Validation And Next Step

The repair must pass the existing host RQ2 gate before publication. The next
fresh KVM execution must not reuse this result root. It must exercise the
withdrawal path after the measured resource record and then complete the full
paired protocol before any timing result is interpreted.

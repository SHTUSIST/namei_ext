# Spindle RQ2 KVM preflight attempt 2

## Scope

This record diagnoses the second real two-boot KVM preflight for the W6
Spindle staging RQ2 comparison. The immutable failed result root is:

```text
results/experiments/spindle-staging-rq2-preflight/20260808T060509Z-w6-rq2-preflight04/
```

The immediately preceding `preflight03` root stopped at host-side vCPU
affinity pinning before the workload began and is recorded separately. This
attempt ran outside that restricted execution context and verified the four
vCPU affinities in both boots.

## Observed result

The `namei_ext` boot completed the source Spindle population, all 47 identity
checks, one warmup, five measured launches, the permission transition, target
withdrawal, preservation, and cleanup. Its final summary reported zero
failures.

The FUSE boot completed the same source population and identity checks, the
warmup, all five measured launches, and all 47 target-engagement checks. The
measured window used kernel passthrough for all 400 regular-file opens, used
zero userspace read fallback, and recorded positive daemon work. The repaired
permission transition executed the non-root child and observed `EACCES`.

The FUSE condition then failed the withdrawal oracle:

```text
invalidate: inode_status=0 entry_status=-ENOENT
withdrawal: exit_status=0 expected_diagnostic=false
target opens: 20 -> 23
```

The run therefore correctly remained failed. Its timing samples are diagnostic
only and support no performance claim.

## Root cause

The FUSE daemon checked the withdrawn bit only in its `lookup` callback. With
the optimized long-lived entry and attribute caches, the post-withdrawal
loader reused the cached FUSE inode and entered the daemon's `open` callback
without another lookup. In this cache state, inode invalidation succeeded but
entry invalidation reported that no named dentry was available to remove.
Accepting that notification status was correct for the earlier permission
transition, but it was insufficient to revoke a cached pathname binding.

## Repair

The first repair made the FUSE `open` callback load the target's withdrawn
state before registering a passthrough backing file. An independent review
blocked that version because an open could read the old state before the
control thread published withdrawal, then complete after the control response.

The reviewed repair uses the inode's existing mutex as the linearization point.
An open reopens the lower file as before, acquires the mutex, reads withdrawn
state, and keeps the mutex through passthrough registration, target-counter
updates, and the successful FUSE reply. Withdrawal obtains a stable inode
reference, acquires the same mutex, publishes the state with a release store,
and only then acknowledges the update. Lookup and open use acquire loads. A
pre-update open therefore finishes before the withdrawal response, while a
post-update open returns `ENOENT` without registering passthrough or increasing
the target-open count. This reuses the lock already taken by every measured
FUSE open rather than adding another steady-state lock.

The repair preserves the optimized FUSE cache during measured launches while
making the baseline responsible for revalidating its dynamic policy when a
cached inode reaches `open`. The withdrawal invalidation event accepts
`entry_status=-ENOENT` when inode invalidation succeeded, but the separate
loader failure and unchanged target-open count remain mandatory.

No workload, baseline, timing, repetition, or interpretation rule changed.
The independent follow-up review returned GO with no remaining P0/P1 finding,
and the complete `spindle-staging-rq2-host-gate` passed after the final repair.
The next gate is the final permitted fresh two-boot preflight under a new
result root.

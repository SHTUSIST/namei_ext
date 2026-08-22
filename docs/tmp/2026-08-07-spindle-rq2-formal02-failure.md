# Spindle RQ2 Formal02 Failure

## Status

The result root
`results/experiments/spindle-staging-rq2/20260808T073352Z-w6-rq2-formal02/`
is a failed formal run and is retained unchanged. It is not paper evidence.
The run completed the first `namei_ext` boot and stopped in the first FUSE
boot because the strict notification gate rejected `-ENOENT` from
`FUSE_NOTIFY_INVAL_ENTRY`.

The source tree was clean at commit `2475071dc410334d33b590ffaaaeaf4fbbfaff`
and the modified kernel was clean at commit
`b07117a3cb41826a34af5ca61e3e2c81dade793f`. The outer run reported failure;
the FUSE boot's prepare, cleanup, inventory, and dmesg checks returned zero,
while its workload command returned 2.

## What The Run Established

The common-lower-filesystem repair worked. Both completed boots recorded
`tmpfs` at the compiled Spindle runtime root. In the completed `namei_ext`
boot, all 50 measured launches incurred zero major faults. Their pooled median
duration was 98.129 ms, with a range of 80.967--188.067 ms. By contrast, every
measured `namei_ext` launch in the rejected formal01 layout incurred 31 major
faults and the pooled median was 247.756 ms. This is diagnostic evidence that
moving both conditions off virtme's uncached 9p tree removed the blocking
lower-filesystem confound. It is not a comparison result because the paired
FUSE cell did not complete.

The stricter FUSE gate also behaved as intended. The mode-removal and
mode-restoration requests each reported:

```text
overall status: -2 (ENOENT)
inode notification: 0
entry notification: -2 (ENOENT)
```

The runner consequently marked the permission transition and condition
summary as failed and terminated the matrix. No failed status was converted
into a passing result.

## Failure Cause

The parent held an `O_PATH` descriptor for the logical object while the daemon
issued notifications, but the daemon sent the inode notification before the
entry notification. The raw result establishes that this order returned zero
for the inode notification and `-ENOENT` for the immediately following entry
notification; it does not establish which internal lookup in
`fuse_reverse_inval_entry()` returned `-ENOENT`. The narrow next test is whether
invalidating the positive parent/name entry first, while the descriptor still
pins the selected inode, lets both notifications complete.

## Next Repair

Keep the `O_PATH` pin and all strict zero-status gates, but issue the entry
notification before the inode notification. This tests the ordering hypothesis
without weakening the baseline or accepting an error. No workload, oracle,
FUSE configuration, metric, repetition count, or comparison budget changes.

After the host gate passes, rerun the complete paired matrix into a fresh
result root. The formal02 root must not be reused or modified.

## Subsequent Outcome

Formal03 falsified the ordering hypothesis: entry-first still returned
`-ENOENT`. Independent review then found that accepting that status with only a
loader failure could miss a stale positive dentry rejected later by FUSE
`open`. The amended protocol therefore adds a non-root `fstatat` pathname probe
that must return `ENOENT` before the loader oracle. The strict-zero plan in this
historical next step is superseded; the formal02 root remains a failed result.

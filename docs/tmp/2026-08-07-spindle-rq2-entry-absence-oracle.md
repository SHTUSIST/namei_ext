# Spindle RQ2 Entry-Absence Oracle

## Motivation

Formal02 and formal03 both observed `-ENOENT` from low-level FUSE entry
invalidation and zero from inode invalidation. The first amendment proposed
accepting that entry status when the existing loader failed after withdrawal.
An independent review rejected the amendment because the loader could fail in
the FUSE `open` callback while a stale positive dentry remained visible to
pathname operations. The review also found that the analyzer did not require
the application oracle rows it claimed to rely on.

## Code Paths Inspected

- `kernel/fs/fuse/dir.c:fuse_reverse_inval_entry()`: `-ENOENT` can arise from a
  missing parent inode, parent alias, parent/name dentry, or positive entry;
- `experiments/spindle_staging/namei_ext_spindle_staging_rq2.c`: FUSE lookup,
  getattr, open-time withdrawal, cgroup/credential probes, raw events, and both
  condition lifecycles;
- `analysis/spindle_staging_rq2/analyze.py`: completeness and correctness gates;
- `mk/experiments/spindle_staging_rq2.mk`: per-boot raw-result checks;
- pinned libfuse 3.18.2 low-level notification API and examples.

## Implementation

The existing child probe now supports two standard operations after moving to
the condition cgroup and dropping to the workload uid/gid: read-only `open`,
used for the permission oracle, and `fstatat(..., AT_SYMLINK_NOFOLLOW)`, used
for pathname withdrawal. After each mechanism withdraws `libtest10.so`, the
`fstatat` probe must return `ENOENT` before the loader failure is attempted.
This distinguishes lookup-visible absence from a later FUSE open-time denial.
Probe setup status and operation errno travel in separate fields: a cgroup
migration or credential error fails the probe and cannot be mistaken for the
expected pathname `ENOENT`.

The raw result adds one `spindle-staging-rq2-withdrawal-lookup` row per
condition and repetition. It records the standard operation and observed and
expected errno. The analyzer now requires exactly one passing row for each of
the following in every condition and repetition:

- non-root permission transition to `EACCES` and successful restoration;
- withdrawn-path `fstatat` returning `ENOENT`;
- withdrawn loader failure with the exact Spindle diagnostic;
- no increase in the mechanism's selected-backing engagement counter.

FUSE inode invalidation must still return zero. Entry invalidation admits only
zero or `-ENOENT`, preserves the raw value, and gains no correctness credit
without all four behavioral records. Every other notification error remains a
hard failure. The per-boot Make gate directly requires the raw withdrawal
engagement window as well as the path, permission, and loader records.

## Host Validation

The runner compiles with `-Werror`. Twelve analyzer tests pass, including
acceptance of `-ENOENT` only with a complete withdrawal oracle, rejection of a
missing or stale withdrawn-path result, and rejection of inode or other entry
notification errors. `make spindle-staging-rq2-host-gate` passes with static
libfuse 3.18.2 and kernel FUSE passthrough retained.

The first follow-up review found that the initial probe encoded setup failure
and syscall errno in one integer and that the Make preflight omitted the raw
withdrawal engagement row. Both issues were repaired as described above. A
second and final follow-up review found no remaining scientific or executability
blocker and returned GO for a fresh paired KVM preflight.

Preflight05 later demonstrated that low-level entry invalidation `-ENOENT` can
coexist with a stale positive dentry in this FUSE implementation. Current code
therefore requires a successful mainline FUSE connection-epoch notification
whenever that status occurs, followed by the same direct `fstatat(ENOENT)` and
application oracles. The earlier status-plus-behavior rule was not weakened.

## Remaining KVM Check

Host tests cannot establish whether `fstatat` observes `ENOENT` on the actual
modified-kernel FUSE path. After independent follow-up review and a clean
commit, a fresh paired KVM preflight must pass all path and application oracles
before the formal matrix is rerun.

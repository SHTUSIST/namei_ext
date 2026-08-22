# Spindle RQ2 Formal03 Failure

## Status

The result root
`results/experiments/spindle-staging-rq2/20260808T074422Z-w6-rq2-formal03/`
is a failed run and remains unchanged. It is not paper evidence. The matrix
completed its first `namei_ext` boot and stopped in its first FUSE boot.

The source tree was clean at commit `06c1a36176ec91259574b858bd48d72e516839be`
and the modified kernel was clean at commit
`b07117a3cb41826a34af5ca61e3e2c81dade793f`. The FUSE boot's prepare, cleanup,
inventory, and dmesg checks returned zero; its workload command returned 2
because the notification gate failed.

## Result

Changing notification order did not change the failure. For both permission
transitions, entry invalidation ran first and returned `-ENOENT`; the following
inode invalidation returned zero. The runner rejected the transition before
executing the independent permission probe. This falsifies the formal02
ordering hypothesis.

Both conditions again recorded `tmpfs` as the compiled-runtime lower
filesystem. The completed `namei_ext` arm passed its source and mechanism
oracles, but the pair is incomplete and has no comparative performance result.

## Primary-Source Correction

The exact-zero entry-notification rule conflates notification status with the
state-transition oracle. Pinned libfuse 3.18.2 passes a low-level entry
notification to the kernel and returns its error. The kernel implementation in
`fs/fuse/dir.c:fuse_reverse_inval_entry()` returns `-ENOENT` when the parent or
positive parent/name entry is absent. This does not by itself prove that all
pathname observations have transitioned, and libfuse's high-level
`fuse_invalidate_path()` contract is not applicable to this low-level entry
operation.

Therefore, exact zero is not the whole feature-equivalence test, but neither is
`-ENOENT` evidence that the view changed. Correctness must combine bounded
notification statuses with direct pathname and application behavior:

- the inode notification used for backing-mode changes succeeds;
- an entry notification returns either zero or `-ENOENT`;
- after every state change, the application-level permission oracle observes
  `EACCES` and a non-root `fstatat` of a withdrawn pathname observes `ENOENT`;
- the withdrawn loader fails with its exact diagnostic and does not engage the
  backing object;
- any other notification error fails the run.

The path and application oracles are decisive together. The `fstatat` probe
catches a stale positive dentry even if the FUSE `open` callback would reject a
later loader request.

## Plan Amendment Required

Revise the existing comparison plan and analyzer to follow the observed kernel
semantics and require a complete behavioral oracle, while preserving raw
component statuses. This is a baseline correctness correction, not a new
hypothesis or a weaker workload. The paired matrix, common tmpfs lower
filesystem, cache settings, passthrough mode, samples, repetitions, and primary
metric remain unchanged.

The amended plan must receive an independent review before another full run.
Formal03 must not be reused or modified.

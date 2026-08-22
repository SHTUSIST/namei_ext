# Spindle RQ2 Preflight04 Failure

## Run

- Result root: `results/experiments/spindle-staging-rq2-preflight/20260808T081532Z-w6-rq2-preflight04/`
- Source commit: `2812ef3e8a3de7ec2b4af8af1f365876d259bdd1`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Terminal status: failed during the first, `namei_ext`, guest boot

The result root is immutable and must not be reused.

## Observed Evidence

Guest preparation, cleanup, post-run inventory, dmesg checks, and vCPU affinity
all passed. The guest ran `7.1.0-rc7-gb07117a3cb41` and copied the Spindle
runtime to a guest-local tmpfs. All 47 focal objects had positive policy-hit
deltas. The source run, one warmup, five measured loader runs, object-identity
checks, and the non-root `EACCES` permission oracle passed.

The new withdrawal lookup oracle failed:

```json
{"event":"spindle-staging-rq2-withdrawal-lookup","condition":"namei_ext","operation":"fstatat","observed_errno":0,"expected_errno":2,"pass":false}
```

The following withdrawal-window row reports `after: 0` only because the runner
returned immediately after the failed lookup probe and never collected the
post-withdrawal counter. It is not evidence that the counter decreased.

## Cause

The namei_ext condition implemented withdrawal by deleting the component-map
entry for `libtest10.so`. In `spindle_staging.bpf.c`, a missing rule returns
`BPF_NAMEI_EXT_PASS`. The lower source file still exists, so normal VFS lookup
makes it visible again. The experiment therefore confused stopping redirection
with withdrawing a pathname.

The FUSE condition removes the entry from its presented view. A
feature-equivalent namei_ext condition must install an explicit
`BPF_NAMEI_EXT_HIDE` decision for the same component, not delete the rule.

## Next Attempt

Extend the Spindle policy's existing component map with one reserved value for
`HIDE`, use it for the withdrawal transition, and require the policy hide
counter plus the existing `fstatat(ENOENT)`, exact loader diagnostic, and
backing-engagement checks. Rebuild and pass the host gate before using a new
preflight result root.

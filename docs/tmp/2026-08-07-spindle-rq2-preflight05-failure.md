# Spindle RQ2 Preflight05 Failure

## Run

- Result root: `results/experiments/spindle-staging-rq2-preflight/20260808T082417Z-w6-rq2-preflight05/`
- Source commit: `f73adc7b260d544b431366b7617c6270b01b1a22`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Terminal status: failed in the second, FUSE, guest boot

The result root is immutable and must not be reused.

## Namei_ext Result

The namei_ext boot completed with all correctness, engagement, cleanup, and
dmesg checks passing. In particular, the repaired withdrawal produced:

```json
{"event":"spindle-staging-rq2-withdrawal-lookup","condition":"namei_ext","operation":"fstatat","observed_errno":2,"expected_errno":2,"pass":true}
{"event":"spindle-staging-rq2-withdrawal-window","condition":"namei_ext","before":21,"after":21,"hide_before":0,"hide_after":4,"pass":true}
```

This is valid preflight evidence for the explicit-hide repair, but the paired
experiment did not complete and is not a paper performance result.

## FUSE Failure

The FUSE arm passed source population, 47-object identity and engagement,
permission, one warmup, five measured loader launches, passthrough, resource,
cleanup, and dmesg checks. All three low-level entry invalidations returned
`-ENOENT`; their inode invalidations returned zero. After withdrawal, the
direct pathname oracle still found the cached entry:

```json
{"event":"spindle-staging-rq2-fuse-invalidation","phase":"withdraw","status":0,"inode_status":0,"entry_status":-2,"pass":true}
{"event":"spindle-staging-rq2-withdrawal-lookup","condition":"fuse","operation":"fstatat","observed_errno":0,"expected_errno":2,"pass":false}
```

The selected-backing open counter remained `20 -> 20`, so the failed stat used
an already cached positive FUSE dentry/inode rather than issuing a new FUSE
lookup or open. This confirms that low-level entry-notification `-ENOENT` is not
an idempotent success for this implementation and cached state.

## Repair Direction

The pathname oracle must remain unchanged. The FUSE baseline should retain its
per-entry notification attempt, but when the mainline API reports `-ENOENT`, it
must use the mainline FUSE connection-epoch notification to invalidate cached
objects before the application probe. The raw result must record whether that
fallback ran and its return status. A new preflight may proceed only after the
runner, analyzer, and direct Make gate require a successful epoch fallback for
the `-ENOENT` case.

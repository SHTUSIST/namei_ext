# Spindle RQ2 Preflight06 Result Review

## Run

- Result root: `results/experiments/spindle-staging-rq2-preflight/20260808T083250Z-w6-rq2-preflight06/`
- Source commit: `c764e37d78091f3908e0644bb93b7717915ffe85`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Matrix: one fresh-boot condition pair, one warmup and five measured launches
  per condition
- Terminal status: completed

The result root is immutable. Preflight timing is not paper evidence.

## Correctness And Engagement

Both modified-kernel guests completed preparation, execution, cleanup,
inventory, dmesg, and vCPU-affinity checks. Across 452 aggregate observation
rows, no row has `pass` other than true.

- both source-population runs used the official Spindle path and produced 47
  distinct mappings;
- 94 logical identity rows are byte-equal to their selected cache objects;
- 94 source/cache preservation rows and 94 target-engagement rows pass;
- all 12 warmup and measured loader launches exit zero with the exact 44-line
  source transcript;
- both permission probes observe `EACCES` and restore the original mode;
- both withdrawn-path probes observe `ENOENT`, and both withdrawn loaders exit
  255 with the exact missing-library diagnostic;
- namei_ext selected-target hits remain `21 -> 21` while lookup-hide decisions
  increase `0 -> 4`;
- FUSE backing opens remain `20 -> 20` during withdrawal;
- all three FUSE entry invalidations return `-ENOENT`, trigger the mainline
  epoch fallback, and record `epoch_status=0` and `inode_status=0`;
- the measured FUSE window records 1,325 callbacks, 400 passthrough opens, zero
  userspace read fallback, zero passthrough failures, and positive daemon CPU;
- the measured namei_ext window records 340 target selections, exactly equal
  to the sum of per-target hit deltas.

No BPF attachment, FUSE mount, `/dev/fuse` user, temporary root, or experiment
cgroup remains after either guest. Both dmesg scans are clean.

## Preliminary Timing

The single preflight pair has a namei_ext median of 79.120 ms and a FUSE median
of 82.217 ms over five measured launches each. This only confirms that the
formal effect is measurable; one pair cannot estimate uncertainty or support a
performance claim.

## Independent Review

An independent read-only claim-to-code-to-raw-evidence review found no missing
oracle, incomplete boot, post-completion mutation, cleanup failure, or analyzer
mismatch. It explicitly excluded preflight timing from paper evidence.

Final verdict: **GO** for the fresh 20-boot formal matrix.

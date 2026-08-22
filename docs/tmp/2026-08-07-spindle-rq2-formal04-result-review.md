# Spindle RQ2 Formal04 Result Review

## Run And Verdict

- Result root:
  `results/experiments/spindle-staging-rq2/20260808T084214Z-w6-rq2-formal04/`
- Source commit: `2483ad450d05791d10219faed69ce7fc9e59aed9`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Matrix: ten alternating fresh-boot pairs, three warmups and 50 measured
  loader launches per condition and boot
- Terminal status: completed

The run is valid for its same-oracle Spindle-derived comparison. The tested
loader-latency hypothesis is **inconclusive**: the FUSE/`namei_ext` geometric
mean of paired boot medians is 1.0104 with paired-bootstrap 95% confidence
interval [0.9668, 1.0491]. The interval includes one, so this result supports
neither a `namei_ext` speedup nor a regression for this loader workload.

This result root is complete and immutable. Formal01--formal03 remain separate
rejected roots and are not included in this result.

## Correctness And Mechanism Evidence

All 20 modified-kernel guests completed preparation, the workload, cleanup,
post-run inventory, dmesg, and vCPU-affinity checks. The aggregate result
contains 1,000 measured launches and 60 warmups with no failed observation.

- Every boot reproduced 47 Spindle-populated source-to-cache mappings. All
  940 mapping, selected-target, byte-identity, and preservation rows passed.
- Every warmup and measured launch exited zero with the exact 44-line source
  loader transcript.
- All 20 non-root permission probes observed `EACCES` after the selected
  cache object was changed to mode zero and passed after restoration.
- After withdrawal, all 20 fresh non-root `fstatat` probes observed `ENOENT`.
  All 20 loaders then exited 255 with the exact `libtest10.so` diagnostic.
- In every `namei_ext` boot, the withdrawn target's selected-hit counter did
  not advance and the lookup-hide counter changed from zero to four.
- In every FUSE boot, the withdrawn target's backing-open counter did not
  advance. All 30 entry notifications returned `-ENOENT`, invoked the
  mainline connection-epoch fallback successfully, and paired with successful
  inode invalidation and the application-level absence checks above.
- The measured windows contain 68 `namei_ext` selections per launch and 265
  FUSE callbacks per launch. Every FUSE regular-file open used negotiated
  kernel passthrough; userspace read fallback and passthrough failures were
  zero.
- No experiment BPF attachment, FUSE mount, cgroup, process, or temporary tree
  remained after a boot, and every dmesg gate passed.

These observations support feature equivalence for the tested final-object
selection, permission, and withdrawal behavior. The callback and selection
counts quantify mechanism engagement; they do not by themselves establish a
runtime advantage.

## Excluded Secondary Numbers

The generated analysis reports a median 114.190 ms FUSE daemon scheduler
runtime and a FUSE/`namei_ext` total-CPU ratio of 1.0393 with 95% interval
[0.9898, 1.0909]. These are not paper-eligible numbers. FUSE repetitions 2,
7, and 8 have different thread counts before and after the measured window.
The collector sums two independent `/proc/<pid>/task/*` snapshots without
retaining matching thread identities, so it cannot prove that thread creation,
exit, or replacement was subtracted correctly. The affected daemon scheduler
runtime, total CPU, and context-switch results must be excluded.

The generated cold-setup medians are 537.232 ms for `namei_ext` and 4.261 ms
for FUSE. They describe the current prototype runners, not an intrinsic
mechanism comparison: the `namei_ext` interval includes 47 control helpers,
each of which forks, joins the cgroup, and registers one target, as well as BPF
map setup and attachment. This number is not a headline comparison and cannot
support a general setup-cost claim.

## Independent Review

An independent read-only claim-to-code-to-raw-evidence review checked the 20
boots, result cardinalities, application oracles, withdrawal causality,
mechanism counters, invalidation fallback, cleanup, analyzer inputs, and paired
latency computation. It independently reproduced the primary interpretation,
found no blocker for the main latency result, and identified the unstable
daemon-resource aggregation above.

Review judgment:

- run status: valid;
- tested hypothesis: inconclusive;
- research value: supporting correctness and mechanism-engagement evidence;
- paper impact: workload boundary for RQ2, not a performance win;
- next paper decision: do not use W6 to claim lower loader latency, CPU, or
  setup cost than FUSE.

Final verdict: **GO** as a valid latency-inconclusive formal result and
**NO-GO** for any claim that `namei_ext` is faster, uses less CPU, or has lower
setup cost in this experiment.

## Next Experiment Decision

This run answered its planned question and should not be repeated merely to
narrow a result that is centered near one. The next highest-value experiment
is a different source-derived uncertainty: a traditional Bazel action view
against official sandboxfs, or a natural-baseline comparison within a case
that still lacks quantitative benefit evidence. A new run must not reuse the
invalid daemon-resource subtraction; it should use stable process/cgroup CPU
accounting or preserve thread identities.

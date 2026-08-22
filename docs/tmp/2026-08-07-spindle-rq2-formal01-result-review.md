# Spindle RQ2 Formal01 Result Review

## Verdict

**NO-GO for paper evidence.** The result root
`results/experiments/spindle-staging-rq2/20260808T065224Z-w6-rq2-formal01/`
is mechanically complete, but it does not isolate the cost of `namei_ext`
from the cost of the feature-equivalent FUSE policy. Two protocol defects
invalidate the comparison. The root remains immutable and is retained as
diagnostic evidence only.

## Completed Execution

The formal command completed 20 fresh modified-kernel KVM boots arranged as
ten alternating condition pairs. All ten `namei_ext` boots and all ten FUSE
boots completed. The raw result contains 500 measured launches per condition
and 30 warmups per condition. Across 5,380 raw events, no event reports
`pass=false` under the runner's current gates.

The application and mechanism paths executed completely:

- all 1,000 measured launches exited zero with the exact 44-line loader
  transcript;
- every boot reproduced 47 Spindle-created mappings and passed all 47 identity,
  engagement, and preservation rows;
- all 20 permission probes and all 20 withdrawal probes passed their observed
  application behavior;
- `namei_ext` recorded 3,400 focal selections per boot;
- FUSE recorded 3,400 focal passthrough opens and 4,000 total passthrough opens
  per boot, with zero userspace read fallback.

This establishes that the full runner path executes. It does not make the
timing comparison valid.

## Blocking Lower-Filesystem Confound

The guest runtime tree was reached through virtme's uncached 9p workspace
mount. The FUSE condition then mounted a long-lived cached view over the entire
test directory and enabled kernel passthrough, while the `namei_ext` condition
continued to walk the 9p tree directly. FUSE therefore changed page-fault and
caching behavior for the executable and non-policy paths as well as the 47
focal objects.

The raw samples expose a deterministic asymmetry: all 500 measured
`namei_ext` launches incurred exactly 31 major faults, while all 500 FUSE
launches incurred exactly 4. FUSE intercepted 80 opens per launch although 68
were focal-object opens. Alternating boot order cannot remove this systematic
filesystem-layout difference.

The analyzer correctly recomputed a FUSE/namei_ext geometric-mean median
latency ratio of 0.860333 with paired-bootstrap 95% CI
[0.839555, 0.879792]. Conditional on this layout, FUSE was 13.97% faster in
wall time. That number is not attributable to policy placement and must not be
used in the paper.

The resource data is consistent with a cache/wait explanation. Mean client CPU
per launch was 30.380 ms for `namei_ext`. After adding measured FUSE daemon CPU,
FUSE used 32.949 ms per launch, 8.46% more total CPU despite lower wall time.
This is useful diagnostic evidence but does not repair the primary comparison.

## Blocking Invalidation Mismatch

The approved plan requires successful FUSE inode and entry invalidation for
the permission and withdrawal state changes. All inode notifications returned
zero, but all 30 entry notifications returned `-ENOENT`. The current runner
accepts that status and relies on its independent open-time withdrawal check.
The application behavior is correct, but the frozen notification requirement
was not met. The analyzer only tests the runner's resulting `pass` fields and
therefore did not independently reject this mismatch.

## Required Repair

The next result must use a fresh root and rerun every comparison cell after two
changes:

1. mount a guest-local tmpfs at the compiled runtime root and copy the pinned
   Spindle runtime tree into it before either condition runs; record and require
   `tmpfs` as the lower filesystem for both conditions;
2. pin the affected FUSE dentry with an `O_PATH` descriptor while issuing the
   inode and entry notifications, and require both notification statuses to be
   zero for permission, restoration, and withdrawal.

The analyzer must also check the exact notification statuses and lower
filesystem record directly. Planned requester CPU, FUSE daemon CPU, callback,
policy-invocation, p50, and p95 outputs should be emitted so that the rerun can
explain the primary result without ad hoc post-processing.

## Independent Review

An independent read-only result review recomputed the stored ratio and sample
counts, identified both blocking defects above, and concluded that formal01 is
not valid paper evidence for RQ2. Its scope judgment is adopted here: the root
supports only the diagnostic observation that a cached FUSE view over this
uncached-9p KVM layout had lower wall time but higher total CPU and far fewer
major faults.

## Subsequent Protocol Correction

Formal02 and formal03 showed that pinning and notification order did not make
entry invalidation return zero. Later kernel-source and independent review
established that neither exact zero nor `-ENOENT` alone is a correctness oracle.
The amended protocol admits only zero or `-ENOENT` for the entry component but
also requires a non-root `fstatat` of the withdrawn pathname to return `ENOENT`,
the withdrawn loader to fail with its exact diagnostic, and no selected-backing
engagement. This supersedes item 2 in the historical repair above; formal01
remains rejected because of its lower-filesystem confound.

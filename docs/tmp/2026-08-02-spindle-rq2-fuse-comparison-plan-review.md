# Plan Review: RQ2 Spindle Final-Object Selection Versus FUSE

Date: 2026-08-02

## Scope

The reviewer examined
`docs/tmp/2026-08-02-spindle-rq2-fuse-comparison-plan.md` against the exact RQ2,
the completed W6 source oracle, current RQ2 evidence, and the
`research-experiment-design` plan-review criteria. The review did not alter or
reduce the seven mandatory RQ1 cases W1--W7.

## Initial Review

Initial verdict: **BLOCK**.

The reviewer identified five defects that could invalidate a matched
performance result:

1. libfuse 3.14 `read_buf` was weaker than the runnable libfuse 3.18.2
   low-level path with kernel FUSE passthrough.
2. Long-lived FUSE caches required explicit entry/inode invalidation for
   withdrawal and mode changes.
3. The existing W6 runner's 10 ms child polling could quantize a short loader
   measurement; timing and transcript parsing needed separation.
4. Resource attribution needed requester CPU and FUSE daemon CPU over the same
   measured window.
5. One fresh boot per condition was needed to prevent cross-mechanism cache
   leakage; correctness and baseline-engagement failures had to invalidate a
   run rather than become statistically inconclusive data.

## Repair And Verification

Repository inspection confirmed that the modified Linux 7.1-rc7 kernel has
`CONFIG_FUSE_PASSTHROUGH=y`. The pinned local libfuse 3.18.2 source at commit
`033844748010a3b8265bf1c90b9ae8ffe4cd9ca7` exposes
`fuse_passthrough_open()` and the official `passthrough_hp` implementation.

The plan was revised to require:

- multithreaded low-level libfuse 3.18.2 with negotiated kernel passthrough;
- successful positive backing IDs and zero userspace data-read fallback;
- explicit entry/inode invalidation with acknowledged success;
- non-polling process completion, blocking `wait4`, and raw requester rusage;
- same-window daemon CPU, scheduler, context-switch, and callback deltas;
- ten independent condition pairs comprising 20 fresh KVM boots; and
- hard invalidation of any run with failed correctness, engagement, cleanup,
  or comparison fairness.

## Follow-Up Review

Final verdict: **GO**.

The reviewer found no remaining scientific or executability blockers. No
additional baseline or larger Pynamic/MPI scale was required for this scoped
one-node final-object-selection experiment. Negotiated passthrough capability,
valid backing IDs, and successful invalidations remain explicit implementation
gates for preflight and the formal run.

## Execution-Grounded Amendment Pending Review

Formal02 and formal03 exposed that the review's phrase "acknowledged success"
incorrectly required an entry-notification return of exactly zero. The kernel
reverse-invalidation path can return `-ENOENT` when the parent or positive
parent/name cache entry is absent. The plan was amended to admit only zero or
`-ENOENT` for the entry component, preserve that raw status, and require a
subsequent application-level oracle. Inode invalidation must still return zero,
and every other error still invalidates the run.

The independent amendment review returned **NO-GO** for two reasons. First, the
withdrawn loader could reach the FUSE `open` callback and fail there while a
stale positive dentry remained visible to `stat`. Second, the analyzer did not
require the claimed permission and withdrawal rows for every repetition. The
review also corrected an inapplicable citation to libfuse's high-level
`fuse_invalidate_path()` helper.

The repair adds a non-root `fstatat(..., AT_SYMLINK_NOFOLLOW)` probe after every
withdrawal and requires `ENOENT` before running the loader oracle. The analyzer
now requires exactly one passing permission, withdrawn-path, withdrawn-loader,
and no-backing-engagement record for both conditions in every repetition. The
high-level helper citation was removed; the plan now states only the observed
kernel reverse-invalidation behavior. A follow-up verdict on this repair is
required before another KVM run; the earlier GO does not approve it.

The first follow-up review remained **NO-GO**. It found that the child probe
encoded cgroup/credential setup failures in the same integer as the pathname
errno, so a setup `ENOENT` could be misreported as a successful absence probe.
It also found that the per-boot Make gate did not directly require the raw
no-backing-engagement row. The probe now transmits setup and operation errors
separately, and the Make gate requires `.before == .after` in the withdrawal
window. A second and final follow-up verdict is pending.

## Final Follow-Up Review

Final amendment verdict: **GO**.

The reviewer rechecked the separated setup/operation error transport, the
direct withdrawn-path oracle, analyzer completeness, raw Make gates, and
notification error handling. No scientific or executability blocker remains
for committing the amendment and running a fresh paired KVM preflight.

## Preflight04 Execution Repair

Preflight04 showed that the namei_ext arm deleted its selected-target rule to
implement withdrawal. A missing rule means `PASS`, so the still-existing lower
source file became visible. This contradicted the already-approved withdrawal
oracle and was an implementation defect, not a change to the comparison or
hypothesis. The repair installs the policy's explicit `HIDE` action and requires
both a positive lookup-hide counter delta and no selected-backing hit. The BPF
object, runner, analyzer, and direct Make gate compile and pass their host tests
before another fresh preflight.

## Preflight05 FUSE Repair

Preflight05 passed the repaired namei_ext arm but showed a stale positive FUSE
dentry after low-level entry invalidation returned `-ENOENT`. The direct
`fstatat` succeeded without a new backing open, proving that the return cannot
be treated as an idempotent success in this run. The oracle remains unchanged.
The FUSE arm now retains the per-entry notification attempt and, only for
`-ENOENT`, uses libfuse's mainline connection-epoch notification before the
application probe. Raw output records whether the fallback ran and its status;
the analyzer and Make gate require a successful fallback in exactly that case.

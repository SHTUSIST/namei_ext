# Build Action RQ2 Re-entry Plan Review

## Scope

This read-only review evaluated
`docs/tmp/2026-08-07-build-action-rq2-reentry-plan.md` against the RQ2
paper-value admission, baseline, plan-review, and real-preflight requirements.
It also read the original closed plan, all three failed preflight records, the
official sandboxfs 0.2.0 source and protocol audit, the current runner, and the
existing RQ2 evidence.

The review asked whether a single newly admitted paired preflight is
scientifically legitimate after the old three-attempt protocol closed. It did
not treat another run ID or repaired infrastructure as research progress.

## Initial Verdict

`NO-GO` for two validity defects:

1. The runner forced `sandboxfs --ttl=0s` even though upstream defaults to 60
   seconds and Bazel's original integration did not override that default. The
   action repeatedly enumerates and opens entries, so disabling metadata
   caching could disadvantage the main FUSE baseline on the primary metric.
2. Stage labels alone would not preserve the failure that actually controls a
   run. Action setup, ready, finish, and reap paths reduced nonzero child exit
   or signal status to `EIO`. A new preflight requires the exact phase, action
   A or B, and child exit code or terminating signal.

The review also required a scope correction: the workload is a generated,
controlled Bazel genrule measured after a barrier. It is not the whole-build
macrobenchmark reported in Bazel's sandboxfs evaluation, so a positive result
cannot establish general application build performance.

## Plan Repairs

The plan now requires:

- sandboxfs's upstream default 60-second metadata TTL in the main comparison;
- unique sandbox IDs, matching official create/destroy acknowledgements, and
  final unmount, without an immediate post-destroy lookup oracle that would
  force metadata caching off;
- exact runtime phase, action identity, and child exit code or signal on an
  action failure; and
- interpretation as a controlled Bazel action-path comparison, not a
  reproduction of published whole-build performance.

The workload, two conditions, action command, file sets, timing boundary,
scales, repetitions, uncertainty calculation, and positive/contradictory/
inconclusive rules remain unchanged.

## Admission Decision

The reviewer found the one-preflight re-entry scientifically legitimate. The
three prior attempts produced no paired performance observation and stopped on
deterministic harness defects, so there was no performance-based optional
stopping. The new plan allows one paired preflight and closes W3 if that run
cannot reach both real conditions without changing the frozen experiment.

W3 has higher immediate decision value than W4 because official sandboxfs is
the source-system FUSE answer to the same action-view problem. A positive,
contradictory, or inconclusive result leads to a different RQ2 paper decision.

## Final Verdict

`GO` after the revisions above. No blocking, high-, or medium-severity plan
finding remains. Execution is authorized only after the declared runner and
Make changes pass host validation and an independent claim-to-code review.

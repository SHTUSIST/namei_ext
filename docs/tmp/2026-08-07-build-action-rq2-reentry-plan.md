# Experiment Plan: RQ2 Build Action View Cost Re-entry

## Research Question

- RQ exactly as written in the paper: What is the cost of putting
  programmable policy on the VFS name-resolution path compared with a
  feature-equivalent FUSE policy implementation?
- Specific uncertainty tested here: whether the controlled RQ2 advantage seen
  in the Agent workspace and FxMark results persists in a traditional,
  input-heavy Bazel action when the FUSE condition is the official sandboxfs
  implementation that motivated Bazel's action-view design.
- Why the answer matters: the existing Agent result may be workload-specific,
  FxMark is synthetic, and the completed Spindle comparison is inconclusive.
  The paper still lacks a valid traditional-build macro comparison against an
  official FUSE system.

## Paper-Value Admission

- Planned role: supporting, with decisive value for whether the RQ2 evidence
  generalizes beyond the Agent workload and microbenchmarks.
- Largest credible paper story this experiment could unlock: `namei_ext`
  preserves a source-derived concurrent Bazel action oracle while reducing the
  controlled action-path cost of the official FUSE view used for the same
  existing files.
- Strongest reviewer reject argument addressed: the current FUSE advantage may
  be confined to one controlled Agent lifecycle, while the synthetic results
  do not establish an application-level effect.
- Independent evidence beyond prior runs: two real Bazel 6.5.0 actions,
  official sandboxfs 0.2.0, 64--2,048 declared files per action, private
  concurrent views, and matched setup and action timing in modified-kernel KVM.
- Why this is not already settled: the sandboxfs paper and Bazel report compare
  other hosts and namespace constructors. They do not compare sandboxfs with a
  VFS-resident lookup policy on the same action, kernel, objects, and timing
  boundary. The three earlier project preflights stopped in the harness before
  the sandboxfs arm and produced no comparison.
- Paper decision if positive: add the traditional Bazel/sandboxfs controlled
  action-path result as the RQ2 complement to the Agent and FxMark results. It
  does not establish whole-build performance.
- Paper decision if contradictory: retain W3 correctness, but explicitly bound
  the performance claim to the workloads where `namei_ext` wins; do not claim a
  general macro advantage over FUSE.
- Paper decision if mixed or inconclusive: report no W3 superiority and use the
  scale and setup/action decomposition only to explain where the mechanisms
  become indistinguishable.
- Best alternative: compare W4 ConfigMap publication with AtomicWriter. W3 has
  higher immediate decision value because sandboxfs is an official FUSE
  implementation for the same build-action view and directly answers RQ2;
  W4 primarily measures materialization rather than the central FUSE baseline.

## Re-entry Justification

The original plan in
`docs/tmp/2026-07-29-build-action-rq2-experiment-plan.md` is closed after three
failed preflights. Those attempts are immutable and remain non-evidence. None
entered the sandboxfs measurement arm:

1. the first exposed a guest bpftool whose attach-type table did not match the
   modified kernel UAPI;
2. the second exposed a relative Bazel artifact path that became invalid after
   the action child changed directory; and
3. the third ran both Bazel actions successfully but rejected newline-bearing
   marker files with an exact no-newline oracle.

The bpftool, Bazel-path, and marker defects are now repaired and independently
reviewed. Re-entry is scientifically justified only because the breadth-first
W1--W7 evidence audit still identifies this matched official-system comparison
as the strongest unresolved traditional-workload test of RQ2. It is not a
continuation that resets the old attempt count.

Before a new real run, the runner must preserve the exact stage whose return
value controls failure. For an action failure it must also preserve whether
action A or B exited and the child's exit code or terminating signal. A generic
`run`-stage `EIO` is insufficient. This diagnostic repair does not change the
workload, baseline, metric, scales, repetitions, or interpretation.

The main sandboxfs condition must use the upstream default 60-second metadata
TTL. Unique sandbox IDs prevent cross-sample reuse. A successful destroy
acknowledgement and final unmount are required, but the harness must not demand
that a destroyed path become unresolvable before the official TTL expires.
That immediate lookup is not part of the Bazel action oracle and would force a
baseline-disadvantaging cache configuration.

This re-entry permits one paired real preflight. If it cannot reach both real
conditions without changing the frozen experiment, W3 closes and the next
experiment is W4. A passing preflight authorizes the unchanged formal matrix.

## Expected And Alternative Outcomes

- Expected answer: at 2,048 declared inputs, the paired 95% confidence interval
  for `sandboxfs / namei_ext` barrier-release-to-both-actions-finished time is
  above one, with every correctness gate passing.
- Strongest competing explanation: Bazel and shell execution dominate the
  measured region, or sandboxfs amortizes its FUSE path sufficiently that the
  two conditions are indistinguishable.
- Contradictory result: both mechanisms are correct and engaged, but the
  primary interval is at or below one.

## Published Precedent And Real Assets

- Published precedent: Bazel's sandboxfs evaluation used clean builds,
  repeated runs, sandboxfs-backed execroots, and total build time:
  <https://blog.bazel.build/2018/04/13/preliminary-sandboxfs-support.html>.
- Official baseline: sandboxfs 0.2.0, commit
  `2305d34fe764a64cf4783b43315e6eb5322310d6`.
- Real application: Bazel 6.5.0 Linux x86-64.
- Reused implementation: official sandboxfs create/destroy protocol, existing
  two-action Bazel barrier, the real `cgroup/namei_ext` attachment, and the
  independently reviewed source-derived correctness oracle.
- Necessary glue: the project runner supplies private mount namespaces and
  identical absolute action paths. It does not replace sandboxfs lookup or
  Bazel action execution.
- Protocol difference: this experiment measures a generated, controlled Bazel
  action after a barrier; it does not reproduce the published whole-build
  benchmark and must not be described as whole-build application performance.

## Comparison

- Proposed mechanism: one `namei_ext` program selects each action's registered
  lower root and exposes only its declared names through lookup and readdir.
- Main baseline: official sandboxfs 0.2.0, using its default 60-second metadata
  TTL and one writable mapping per declared input. It represents the source
  system's FUSE answer to arbitrary action views.
- Need for a matched run: published numbers cannot establish relative cost on
  this kernel, action, object set, or timing boundary.
- Controls: mechanism engagement, lower-object preservation, empty pre/post
  inventory, and clean dmesg are validity gates, not extra baselines.
- If sandboxfs matches or wins: W3 supports RQ1 expressiveness only and provides
  no performance advantage for the tested build action.
- Fairness: both arms use the modified kernel, identical lower objects, Bazel
  command, file set, concurrency, CPU/memory allocation, and action boundary.
  Neither arm receives undeclared-file names.

## Workload, Metrics, And Runs

- Workload: two concurrent Bazel genrules. Each enumerates its action view,
  rejects undeclared names, reads all declared files in lexical order, and
  emits the complete ordered contents as its aggregate output.
- Scales: 64, 512, and 2,048 declared files plus the same number of physically
  existing undeclared files per action. The 2,048-file cell is primary.
- Primary metric: paired ratio of sandboxfs to `namei_ext` time from barrier
  release until both action-finished records appear.
- Secondary metrics: action time at 64 and 512 inputs, view setup time, and
  lifecycle time. Daemon resource metrics are usable only if process/thread
  identity is stable across snapshots.
- Correctness: both actions overlap and execute; the complete ordered output
  bytes match the independently generated expected transcript; declared files
  are visible; undeclared and post-setup files are hidden from lookup and
  readdir; lower files are unchanged; each mechanism is engaged; every
  sandboxfs create/destroy request receives a matching successful
  acknowledgement; final teardown and dmesg checks pass.
- Preflight: one paired 64-input sample plus the 4,096-entry maximum-scale map
  capacity probe.
- Formal run after a passing preflight: ten alternating paired boot blocks,
  three samples per scale, 20 fresh KVM boots and 180 lifecycle samples.
- Uncertainty: per-boot medians, paired ratios, and a deterministic 10,000
  resample percentile-bootstrap 95% confidence interval.

## Execution And Interpretation

- Preflight command:
  `make kvm-build-action-rq2-preflight RUN_ID=<fresh-id>`.
- Formal command:
  `make experiment-build-action-rq2 RUN_ID=<fresh-id>`.
- Raw roots: `results/experiments/build-action-rq2-preflight/<RUN_ID>/` and
  `results/experiments/build-action-rq2/<RUN_ID>/`.
- Preflight completion: one immutable paired root in which both real
  mechanisms pass the same 64-input oracle and the maximum-scale capacity gate
  passes.
- Formal completion: exactly 20 completed boot roots, 180 passing samples, ten
  complete pairs per scale, no replaced cells, and a successful analysis.
- Positive: primary confidence interval is above one.
- Contradictory: primary confidence interval is at or below one.
- Inconclusive: primary interval crosses one.
- Target figure: action and setup time versus declared-input count, with the
  primary paired ratio and confidence interval stated numerically.

## Implementation And Validation Before KVM

- Runner inspected:
  `experiments/build_action_sandboxing/namei_ext_build_action_rq2.c`.
- Required edits: propagate a specific stage label for each fallible runtime
  phase; preserve action identity and raw child exit/signal status; use the
  upstream sandboxfs TTL default; and remove the harness-only immediate
  post-destroy lookup check.
- Required host validation: warning-clean runner build, existing Build Action
  analyzer and infrastructure tests, and an independent claim-to-code review
  of the three repaired historical defects and the new failure provenance.
- Project constraint: the rerun must not execute or regenerate checksum
  manifests or checksum gates. The action oracle therefore compares complete
  ordered output bytes rather than a digest. Source commits, software versions,
  runtime identities, raw observations, and semantic oracles provide the
  experiment evidence.
- Remaining risk: official sandboxfs has never completed one real arm in this
  harness. That exact uncertainty is why the single paired preflight remains a
  dependency rather than paper evidence.

# W4 Quantitative Experiment Plan Review

## Scope

This review gates implementation of
`docs/tmp/2026-08-07-kubernetes-configmap-quantitative-plan.md`. It asks whether
the planned comparison between Kubernetes v1.30.0 `AtomicWriter` and
`namei_ext` is fair, measurable, source-faithful, and useful to the paper. It
does not review or change the paper's RQ wording.

## Round 1: No-Go

The independent review found six implementation blockers:

1. The complete-lifecycle timer did not identify exact start and end states.
2. Timed application work was conflated with exhaustive correctness auditing.
3. Post-state inventories could not support cumulative object-churn claims
   after `AtomicWriter` deleted an old timestamp tree.
4. "Real service lifecycle" overstated the two-known-generation,
   post-acknowledgement subset.
5. Payload width, generated entries, and change fraction were not defined.
6. Ten boots were too weak for the planned bootstrap superiority gate.

The plan was revised to:

- start before any condition-specific filesystem or BPF-map mutation and end
  after an identical rollback consumer acknowledgement;
- use one persistent consumer operation sequence in both conditions and move
  exhaustive inventories, controls, result encoding, cleanup, and dmesg outside
  the primary timer;
- preserve each current `AtomicWriter` timestamp-tree observation before the
  next call can remove it, while reporting live namespace inventories rather
  than unsupported cumulative symlink counts;
- limit feature equivalence to two complete generation inputs known before the
  lifecycle and exclude unseen updates, notification, reload, and kubelet
  propagation;
- define `N` as union leaf paths with deterministic names, bytes, modes, and
  four changed paths at `N=4,16,64,256`; and
- use 20 independent boots, counterbalanced pair and scale order, boot-level
  median log-ratios, and an upper 95% interval below 1.0 as the superiority
  rule.

## Round 2: No-Go

The second review found two specification contradictions:

- the timer text simultaneously assumed an existing per-volume root and timed
  creation of that root; and
- the target table still called live namespace inventory "objects created."

The plan now starts with one existing fresh per-sample parent while all
condition-specific roots and the per-volume cgroup are absent. The target table
reports live visible objects after each state.

## Final Review

The independent final review returned **GO**. The timer state and namespace
inventory terminology are consistent, and no implementation-blocking issue
remains.

Implementation remains subject to a separate claim-to-code-to-raw-evidence
review before the real preflight. A mechanically completed Make target will not
be treated as a scientifically successful result without that review.

## Implementation Admission Review

The first implementation admission review returned **NO-GO**. It found that
full consumer JSON serialization remained inside the measured interval, source
materialization audits could run before later timed conditions, phase sums and
the exact Cartesian matrix were not independently validated, cleanup and old-FD
evidence were summarized rather than preserved, source metadata named the wrong
adapter, failed result roots could be re-entered, and CPU-time comparisons were
not equivalent between the two mechanisms.

The implementation removed CPU-time comparisons, changed timed consumer output
to a bounded acknowledgement, deferred full evidence export until after the
rollback timer, separated source audits from every timed condition, validated
the exact boot/pair/scale/order matrix and every phase sum, preserved exact
cleanup and descriptor observations, corrected source metadata, and added
immutable execution/finalization/analysis markers.

A second review identified two remaining blockers: source audit work still
preceded the next timed condition, and the analyzer still used the active-phase
sum rather than the frozen begin-to-end wall span. The guest now completes the
entire timed matrix before any source audit, and `wall_span_ns` is the analyzer's
primary metric with a dedicated regression test. The independent final reread
returned **GO** for exactly one modified-kernel KVM preflight.

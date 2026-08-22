# Kubernetes ConfigMap Setup Attribution And Batching Plan

## Motivation

The third W4 preflight is a failed, immutable result and cannot support a paper
performance claim. It nevertheless exposes a concrete implementation question
that must be resolved before the formal run. At width 256, more than 99% of the
observed `namei_ext` lifecycle elapsed inside the aggregate setup interval. The
current record cannot attribute that interval to file preparation, target
registration, BPF map population, or consumer cgroup migration.

The code also reveals a plausible avoidable cost. W4 registers 510 existing
lower objects at width 256. `namei_ext_register_target()` currently performs one
`fork()`, one cgroup migration, one debugfs open/write, and one polling wait per
object. The polling loop sleeps for 10 ms whenever the child is not complete at
its first probe. This process pattern is a candidate bottleneck, not yet a
measured attribution.

## Hypothesis

A single control child can enter the policy cgroup once and register the same
ordered set of existing target objects through the existing debugfs operation.
This preserves the kernel ABI, target IDs, retained `struct path` objects,
cleanup behavior, workload oracle, and primary begin-to-end timer while removing
per-target process creation and polling.

## Measurement Contract

The existing `wall_span_ns` remains the primary metric and its boundaries do
not move. The existing `setup_ns` remains part of `active_total_ns`. Each
successful `namei_ext` lifecycle row gains four adjacent, positive setup
components whose exact sum must equal `setup_ns`:

1. `object_preparation_ns`: logical/lower directory creation, lower and logical
   file creation, cgroup creation, and cgroup-ID lookup.
2. `target_registration_ns`: construction and registration of all existing
   target objects.
3. `map_population_ns`: population of both generation view maps.
4. `consumer_cgroup_move_ns`: moving the already-running consumer into the
   policy cgroup.

The row also records `registered_targets`, which must equal
`2 * (width - 1)`. Validation rejects missing, nonpositive, nonadditive, or
incorrectly counted setup evidence. The analyzer reports these fields in the
decomposition JSON, CSV, and Markdown report. AtomicWriter remains unchanged;
the additional fields are specific to the proposed mechanism.

## Implementation

- Stage A adds `namei_ext_register_target_batch()` to the shared harness. Its caller
  supplies an ordered array of path/ID pairs. The parent validates the array,
  then one child enters the target cgroup, opens the existing
  `register_target` debugfs file once, and processes every pair. The existing
  one-target API becomes a one-element wrapper.
- Stage A also links the shared harness into the existing functional-test
  binary. A new control-path sequence on the modified kernel must cover:
  multiple successful entries written through one control fd; a missing path
  after at least one successful entry; `clear_targets()` after that partial
  failure; policy-observed absence of all cleared IDs; and reuse of the same IDs
  after clearing. The same boot also runs five AB/BA pairs at 64 targets. Every
  arm receives a distinct newly created cgroup and therefore an empty target
  registry; both arms register the same ordered path/ID set, then clear the
  registry and remove the cgroup. This prevents the later arm from replacing
  existing IDs and paying per-target `synchronize_rcu()`. Raw per-arm times and
  paired ratios are retained, and the median paired batch/scalar ratio must be
  below one. This is a control-path diagnostic, not a W4 workload run or paper
  result.
- Stage B builds the W4 registration array on the heap, because 510 `PATH_MAX` buffers
  do not belong on the stack. Preserve partial-failure cleanup by marking
  targets registered before invoking the batch and clearing the cgroup's target
  set on every later exit.
- Take one monotonic timestamp at each adjacent setup boundary. Reuse each end
  timestamp as the next phase boundary so the four components sum exactly to
  the unchanged aggregate setup interval.
- Extend validator and analyzer tests with exact count, additivity, missing
  field, and emitted decomposition-column checks.

## Rejected Alternatives

- Do not remove target registration from `wall_span_ns`; that would change the
  workload contract instead of reducing its implementation cost.
- Do not register only a directory target or change policy semantics; W4's
  source oracle checks per-file selected-object identity, presence, absence,
  modes, and persistent descriptors across generations.
- Do not add a new kernel batch ABI. The existing debugfs control operation
  already supports repeated writes by one cgroup-scoped process, so the first
  repair belongs in the userspace harness.
- Do not replace the source-derived AtomicWriter baseline or reduce width 256.
- Do not use the failed preflight timing in the paper or run a fourth W4
  preflight.

## Validation Gates

1. Shared harness and W4 binaries compile with warnings as errors.
2. Existing host source, success, and failure gates pass.
3. Validator/analyzer tests cover exact setup decomposition and count.
4. A modified-kernel `kvm-functional` gate executes multi-entry success,
   repeated writes to one control fd, mid-batch failure after a successful
   prefix, complete clear, policy-observed absence, ID reuse, and the same-boot
   five-pair scalar-vs-batch diagnostic with fresh per-arm cgroups and AB/BA
   order. Formal W4 execution cannot be the first real exercise of any batch or
   rollback path.
5. Stage A is committed and pushed only after that KVM gate and raw-evidence
   review pass. Stage B then integrates the reviewed API into W4 and adds timing
   decomposition in a separate commit.
6. An independent claim-to-code review checks that timing boundaries and
   correctness oracles did not move, and that partial registration is cleared.
7. Only after those gates pass may the frozen 20-boot W4 formal experiment run.

## Remaining Risk

Batching may expose map population or filesystem preparation as the next setup
limit. That is a measurement outcome, not a reason to alter the hypothesis or
timer. The setup decomposition will identify the next code path, while the
formal experiment remains responsible for the complete lifecycle comparison.

## Admission Review

The first independent review rejected a one-element-only functional gate because
it did not execute the new multi-entry or partial-failure paths. The second
rejected a same-registry scalar/batch comparison because reused target IDs would
make the later arm pay replacement-time RCU synchronization. The plan now uses
an explicit multi-entry KVM correctness gate and fresh per-arm cgroups in five
AB/BA diagnostic pairs. The final review found no remaining blocker and returned
**GO** for Stage A, followed by raw-evidence review before Stage B.

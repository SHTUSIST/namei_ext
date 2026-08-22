# Kubernetes ConfigMap Setup Batching Implementation

## Motivation

The Stage A control-path gate established that the shared harness's per-target
process/wait pattern was avoidable: a 64-target batch completed with a median
paired ratio of 0.015632935 relative to the scalar wrapper, while exercising
the same kernel insertion operation. That result is diagnostic rather than W4
or paper evidence. Stage B applies the reviewed API to W4 without changing its
source workload, baseline, correctness oracle, primary timer, or formal matrix.

## Implementation

`namei_ext_configmap_quantitative.c` now constructs one ordered registration
array for every present v0 and v1 lower object. The array and `PATH_MAX` backing
storage are allocated on the heap. The runner verifies exactly
`2 * (width - 1)` entries, invokes the reviewed batch helper once, and records
that count only after all targets register. The existing cleanup flag is set
before the call, so any partial batch is cleared on the ordinary error path.

The existing begin-to-end `wall_span_ns` and aggregate `setup_ns` boundaries do
not move. Four adjacent timestamps partition setup:

1. `object_preparation_ns`: directory, cgroup, lower-file, logical-file, and
   cgroup-ID preparation.
2. `target_registration_ns`: registration-array construction and one batch
   control operation.
3. `map_population_ns`: both generation view maps.
4. `consumer_cgroup_move_ns`: moving the running consumer into its policy
   cgroup.

Each boundary timestamp is reused as the next phase's start. Consequently the
four raw integers must sum exactly to the unchanged `setup_ns`; no residual or
rounding term exists.

## Evidence Contract

Every successful `namei_ext` lifecycle row contains `setup_phases` with exactly
those four positive fields and `registered_targets == 2 * (width - 1)`. Source
AtomicWriter rows must contain neither field. The validator rejects a missing,
extra, nonpositive, nonadditive, or incorrectly counted setup record.

New `run.json` roots freeze the primary metric, aggregate setup field, component
order, exact-sum requirement, target-count equation, and single-control-child
registration method. The analyzer independently revalidates additivity and
count, propagates all four phases through pair, boot, and scale summaries, adds
them to `decomposition.csv`, and emits a dedicated setup-attribution Markdown
table. The source baseline remains unchanged.

## Validation

- The W4 binary and shared harness rebuild with `-Wall -Wextra -Werror`.
- Source metadata and upstream AtomicWriter tests remain pinned to Kubernetes
  v1.30.0 commit `7c48c2bd72b9bf5c44d21d7338cc7bea77d0ad2a`.
- Host success and declared failure paths pass.
- Nineteen validator/analyzer tests pass, including exact sum, exact target
  count, frozen setup protocol, analyzer rejection, CSV column, and Markdown
  section checks.
- `git diff --check` and Python bytecode compilation pass.

No fourth W4 preflight will run. An independent claim-to-code review verified
the unchanged timer and baseline, exact phase coverage, heap lifetime, partial
cleanup, analyzer propagation, and formal `run.json` contract. It found no
blocker and returned **GO** for both the Stage B commit and the post-commit
20-boot formal run. After this commit is pushed, the next W4 execution is that
frozen formal run.

# Build Action RQ2 Re-entry Implementation

## Purpose

This step repairs the W3 paired experiment harness before its one admitted new
KVM preflight. The experiment compares the same controlled Bazel genrule over
two path-view mechanisms: `namei_ext` and the official sandboxfs 0.2.0 FUSE
implementation. This is an implementation record, not a performance result.

The repairs address the three failures that invalidated the old preflight
sequence and the two blockers found by the independent re-entry review:

- use the guest kernel's matching bpftool for the real `cgroup/namei_ext`
  attach path;
- pass an absolute Bazel executable path across the action `chdir`;
- compare exact marker contents rather than accepting a newline mismatch;
- preserve the action phase, A/B identity, and raw child exit status that
  controls a failure; and
- finalize the W3 result from semantic and provenance evidence without
  requiring checksum files that this experiment no longer creates.

## Source And Protocol Inspection

The implementation was checked against these paths:

- `experiments/build_action_sandboxing/namei_ext_build_action_rq2.c`;
- `bpf/policies/build_action_sandboxing.bpf.c`;
- `mk/experiments/build_action_rq2.mk`;
- `mk/workload.mk` and `configs/benchmarks/workload-sources.mk`;
- sandboxfs commit `2305d34fe764a64cf4783b43315e6eb5322310d6`, its
  command protocol, and its Cargo lock file; and
- `analysis/build_action_rq2/analyze.py` and its tests.

The official sandboxfs arm now uses the upstream 60-second metadata-cache TTL.
Each sample uses unique sandbox IDs, requires matching create and destroy
acknowledgements, and performs a final unmount. It no longer checks that a
destroyed path disappears immediately, because that check would require
disabling the upstream cache and would change the measured FUSE condition.

## Workload Equivalence And Oracle

Both conditions launch the same two Bazel processes, use separate cgroups and
backing trees, stop at the same setup barrier, and run the same
single-threaded standalone genrule after the same release boundary. Only the
path-view mechanism changes.

The action writes its sample ID followed by the complete ordered contents of
all declared inputs. The runner constructs the expected transcript separately
from the backing files and compares every output byte. It also checks action
markers, lower-tree contents and metadata, policy engagement for `namei_ext`,
FUSE engagement for sandboxfs, and cleanup after each boot. Output digests are
not part of the oracle.

## Failure Provenance

Each fallible sample phase sets the stage that owns its return value. Action
setup, release, ready, completion, and reap paths retain an explicit pointer to
action A or B. Child collection records the raw `waitpid` status before the PID
is cleared; error cleanup uses an action-specific terminator that also records
the collected status. Failure JSON reports the stage, action name, exit code,
and terminating signal.

The runner ignores `SIGPIPE` only in the parent so a closed control pipe is
reported as an error instead of terminating the collector. Bazel, sandboxfs,
and fusermount children restore the default `SIGPIPE` behavior before `exec`.

## Result Lifecycle

W3 now has a local run validator that checks the active run schema, source and
kernel commits and dirty state, command, raw observations, boot matrix,
per-boot completion, correctness rows, policy counters, capacity probe, CPU
pinning evidence, and host environment records. It does not call the legacy
base validator that requires `inputs.sha256` and `artifacts.sha256`.

The W3 host capture records CPU topology and frequency policy, `virtme-ng`
version and resolved paths, before/after host counters, kernel commit/release
and build ID, runtime versions, raw logs, and copied runtime artifacts. The W3
target and its sandboxfs/Bazel dependency path do not create or validate
checksum manifests. Both W3 entrypoints explicitly depend on
`kernel-provenance`, which verifies that the source commit, built-kernel commit,
release string, and captured `KERNEL_COMMIT_FILE` agree before artifact capture.

## Validation Completed

- `make build-action-sandboxing`: passed with `-Wall -Wextra` and no warnings.
- `make build-action-rq2-analysis-test`: 9 tests passed.
- `make -C tests/infrastructure published-result-contract`: 29 tests passed,
  followed by successful validation of existing published formal bundles.
- `make workload-sandboxfs-build`: passed for sandboxfs 0.2.0 built with
  `cargo build --release --locked`, Rust 1.90.0, and libfuse 2.9.9.
- `make workload-bazel`: passed for Bazel 6.5.0.
- `git diff --check`: passed.

No new KVM result has been produced at this step.

## Independent Review

The first implementation review returned NO-GO because W3 still called the
legacy base validator after removing its required checksum files, and because
setup/release failures could lose the action identity and raw wait status. The
repair also removed the overlooked checksum operation in the shared pinned-host
capture from W3's executed path.

The follow-up review found one further high-priority dependency defect: W3 read
`KERNEL_COMMIT_FILE` without explicitly depending on `kernel-provenance`, so a
fresh tree could fail and a stale tree could record the wrong kernel commit.
Both entrypoints now declare that dependency. A final follow-up traced the
parsed Make dependencies and modified-kernel provenance target and returned GO
with no remaining blocker, high, medium, or low findings.

## Remaining Risks

- The complete repaired host-to-guest path has not yet been exercised by the
  new real KVM preflight.
- The preflight must reach and pass both the `namei_ext` and sandboxfs arms;
  host-only validation is not Phase 1 evidence.
- A passing preflight permits the frozen 20-repetition formal run. It does not
  itself support a performance claim.

# Build Action RQ2 Re-entry Preflight Result

## Verdict

The paired KVM target completed, but the result is **not an admitted successful
preflight**. It proves that both repaired real mechanism paths execute. It does
not satisfy the complete frozen correctness oracle, so it does not authorize
the 20-boot formal experiment and supports no performance claim.

The immutable result root is:

`results/experiments/build-action-rq2-preflight/20260808T102201Z-w3-rq2-reentry-preflight01/`

The captured source commit is `6700791b63e9ec77929ef3344a5308d4672a7778`;
the captured modified-kernel commit is
`b07117a3cb41826a34af5ca61e3e2c81dade793f`. Both worktrees were clean.

`run.json` records `status=completed` because all executable Make gates then
implemented returned success. The independent claim-to-code-to-raw-evidence
review overrides that mechanical status for scientific interpretation.

## What Executed Correctly

The run contains two fresh four-vCPU KVM boots in the frozen order:
`namei_ext`, then official sandboxfs 0.2.0. Both vCPU mappings were verified
one-to-one on host CPUs 4--7 before workload release.

The `namei_ext` arm had one live program attached as `cgroup_namei_ext` at
`/sys/fs/cgroup`, no FUSE mount or daemon, and no BPF program before or after
the measured run. Its 4,096-entry capacity probe inserted and removed every
entry. All policy counters were present and nonzero where required: 147,726
total decisions, including 136,645 lookup, 11,081 readdir, 4 target selections,
and both lookup and readdir hide decisions.

The sandboxfs arm used captured upstream commit
`2305d34fe764a64cf4783b43315e6eb5322310d6`, sandboxfs 0.2.0, and libfuse
2.9.9. The command did not override its 60-second default metadata TTL. The
middle inventory recorded one live FUSE mount and one `sandboxfs` `/dev/fuse`
holder, with no BPF program. Both disappeared after the run.

Both arms launched the same two Bazel 6.5.0 standalone genrules. Each saved
output contains 65 lines and 2,007 bytes: one sample ID and all 64 declared
file contents in order. Corresponding outputs from the two mechanisms are
byte-identical. Every before/after lower-tree manifest is byte-identical, and
the runner checked each lower-file content. Runner status was zero in both
boots; runner, sandboxfs, and launcher stderr were empty; configured dmesg
failure checks passed.

The observed action times were 538.327 ms for `namei_ext` and 555.109 ms for
sandboxfs, a single paired ratio of approximately 1.031. This is only a sanity
observation from one 64-file sample. It is not an estimate, confidence interval,
or paper result.

## Blocking Oracle Defect

The frozen plan requires a file created after view setup, `unknown.txt`, to be
hidden from both lookup and directory enumeration. The generated action tests
it only with `test ! -e`, which exercises lookup. Its directory-enumeration
check rejects only the pre-existing `undeclared-*` names. Neither the aggregate
namei readdir counter nor the sandboxfs inventory identifies which name was
enumerated.

The runner nevertheless emitted `unknown_hidden=true`, and the analyzer accepts
that boolean as a complete oracle. The two sample rows therefore overstate the
executed evidence. This is a claim-to-code defect, not a noisy timing result.

The review also found that final removal of the runner's temporary tree cannot
fail the run because the shared removal helper returns no status. Policy,
cgroup, mount, daemon, and per-view cleanup were checked and passed, so this is
a lower-priority cleanup-evidence gap rather than the admission blocker.

## Decision

- Do not call this an admitted passing preflight.
- Do not run or report the W3 formal performance matrix from this harness.
- Do not cite the single timing ratio as evidence for RQ2.
- Do not modify, finalize again, or reinterpret the stored result root.
- Do not spend another W3 repair preflight in this breadth-first experiment
  cycle. The re-entry plan admitted one new paired root after three earlier
  harness failures; this root exposed another oracle defect.
- Move to the next admitted industrial comparison, W4 service/config rotation
  against Kubernetes AtomicWriter/materialized publication. W3 can be revisited
  only through a future explicitly reviewed experiment admission, not an
  automatic retry.

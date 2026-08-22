# Target Registration Batch Functional Gate

## Motivation

W4 setup currently invokes the shared one-target control helper once per lower
object. At width 256 this creates 510 child processes and exposes the parent's
10 ms polling interval up to 510 times. Before W4 adopts a batch helper, the new
control path must execute independently on the modified kernel, including its
partial-failure cleanup path. Formal W4 execution must not be its first test.

## Code Paths

- `runner/src/namei_ext_harness.c`: userspace cgroup move, control-child wait,
  target registration, and target clearing.
- `kernel/fs/namei_ext.c`: repeated debugfs writes resolve FDs in the current
  process, retain each `struct path`, insert by cgroup/target ID, and clear all
  entries for one cgroup after one RCU grace period.
- `tests/functional/namei_ext_functional.c`: modified-kernel control and VFS
  behavior oracle.
- `mk/kvm.mk`: guest entry point and raw JSONL gate for `kvm-functional`.

## Implementation

The shared harness now accepts an ordered array of `(path, target_id)` pairs.
The parent validates every pointer and ID before forking. One child enters the
target cgroup, opens the existing debugfs control file once, and opens and
writes each target FD in order. The old API is a one-element wrapper, so callers
retain their existing contract. No kernel ABI or target-registry code changes.

The functional test creates a dedicated cgroup, enters it, and executes these
oracles through the shared harness:

1. Register a valid first target followed by a missing path and require failure.
2. Before clearing, attach the select policy and require target ID 1 to expose
   the registered directory while IDs 2--4 remain absent. This proves that the
   failed call left a real successful prefix rather than failing before its
   first write.
3. Clear the partially populated registry and attach the select policy; all
   four policy-selected pathnames must remain absent.
4. Batch-register directory, regular-file, executable, and pinned-file targets;
   the attached policy must expose all four existing objects.
5. Clear the registry and require all four pathnames to become absent.
6. Reuse the same four IDs and require all four objects to become visible again.
7. Clear on every exit, return the test process to the root cgroup, and remove
   the dedicated cgroup.

The same KVM boot then runs five diagnostic pairs with 64 targets. Scalar and
batch arms alternate order. Each arm uses a distinct newly created cgroup and
empty registry, registers the same path/ID sequence, records elapsed
registration time, clears all targets, and removes its cgroup. This avoids
replacement-time `synchronize_rcu()` and retains per-arm observations, paired
ratios, and a median ratio. The diagnostic gate requires the median paired
batch/scalar ratio below one; it is not a paper result.

The guest-side jq validator rejects duplicate or missing pair/order/mechanism
coordinates, nonpositive timings, failed registration/clear/removal, incorrect
AB/BA order, pair rows that do not reproduce their arm observations, or a
summary whose win count and median do not reproduce the five pairs.

## Failure Handling

Any batch invocation that reaches the child is treated as potentially partial,
including an unexpected error in the nominal success path. The functional test
therefore marks the registry dirty before each call and clears it on every exit.
The harness child closes the target FD after every write and the control FD on
success or error. A batch error remains a hard error; it is never converted into
partial success.

## Validation Status

Host compilation of the shared harness and functional binary passes with
`-Wall -Wextra -Werror`. The structured jq validator accepts a generated valid
matrix before KVM execution.

The modified-kernel gate then passed:

- Command: `make kvm-functional
  RUN_ID=20260808T140941Z-target-batch-functional`
- Raw root:
  `results/phase1/20260808T140941Z-target-batch-functional`
- Source base before this Stage A commit:
  `1d2af5c1f3a21687ea93995178e84e573e26421e`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Guest release: `7.1.0-rc7-gb07117a3cb41`

The JSONL contains 168 rows: 150 functional cases, ten diagnostic arms, five
paired rows, one diagnostic summary, and start/done lifecycle rows. Every row
with a `pass` field has `pass=true`, including the existing functional matrix.
The failed batch exposed only target ID 1 before clear; all four IDs were absent
after clear. Four-target success, clear, and ID reuse all passed. Every
diagnostic arm reported positive elapsed time and zero registration, clear, and
cgroup-removal errors with the exact AB/BA coordinates.

For diagnosis only, scalar registration of 64 targets had per-pair times from
618.8 to 656.1 ms, while batch registration took 10.12 to 13.76 ms. Batch won
all five pairs; the median paired batch/scalar ratio was 0.015632935. These
numbers establish that the shared control helper, rather than the kernel target
insert path, caused the avoidable per-target delay. They are not W4 or paper
performance results.

The guest-side validator independently reproduced every arm, pair, ratio, win
count, and median. The dmesg failure-pattern scan was empty, the functional
summary reported zero failures, every temporary cgroup reported successful
removal, and no QEMU or vng process remained on the host. A final independent
raw-evidence review found no blocker and returned **GO** for the Stage A commit.
Stage B W4 integration remains separate and begins only after this commit is
pushed.

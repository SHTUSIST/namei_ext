# W4 Quantitative Preflight Attempt 01

## Purpose

This record preserves an execution failure before any W4 quantitative workload
ran. It prevents the immutable result root from being mistaken for mechanism or
performance evidence and records the demonstrated launcher failure mode.

## Invocation And Identity

- Command: `make kvm-kubernetes-configmap-quantitative-preflight
  RUN_ID=20260808T124612Z-w4-quantitative-preflight01`
- Source commit: `5657e57744bdcc488a616939a4f76e1278a2d942`
- Kernel commit: `b07117a3cb41826a34af5ca61e3e2c81dade793f`
- Result root:
  `results/experiments/kubernetes-configmap-quantitative-preflight/20260808T124612Z-w4-quantitative-preflight01`

Both source trees were clean when the result root was created. The launcher was
mistakenly invoked through a PTY. The `timeout` child was placed in a process
group different from the PTY foreground process group; `vng`, `virtme-run`, and
QEMU entered stopped state while QEMU's stdin chardev referenced the terminal.
The VM therefore did not reach the guest command.

## Preserved Evidence

The frozen `run.json` records `status=failed` and
`failure=kvm-launch-or-guest-command`. Both launcher logs are empty. There is no
`observations.jsonl`, guest kernel evidence, or lifecycle row. The launcher was
terminated after the stopped process state was diagnosed, and the normal
failure path finalized `run.json` at `2026-08-08T12:49:26Z`.

This root provides no evidence about `AtomicWriter`, `namei_ext`, correctness,
or performance. It must not be repaired, finalized, analyzed, or reused.

## Next Gate

The next attempt will use the standard non-PTY Make invocation so QEMU does not
inherit a controlling terminal. It will use a new result root. No harness,
policy, validator, hypothesis, or experimental matrix changes are admitted on
the basis of this launcher-only failure.

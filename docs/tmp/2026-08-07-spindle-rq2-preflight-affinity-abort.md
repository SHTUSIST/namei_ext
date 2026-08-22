# Spindle RQ2 preflight affinity abort

## Scope

This record covers the fresh W6 Spindle staging RQ2 preflight invocation at:

```text
results/experiments/spindle-staging-rq2-preflight/20260808T060045Z-w6-rq2-preflight03/
```

The invocation used source commit `a6409b82b52b2ce7ac323db0560977293d97be74`
and the clean modified-kernel commit recorded by the result root. The result
root is immutable and is not reused or repaired.

## Observed failure

The run stopped during the first `namei_ext` boot before the guest workload
started. QMP was reachable, but `tools/kvm/pin_vcpu_affinity.py` could not set
the four QEMU vCPU thread affinities to host CPUs 4--7. Its raw result records
200 attempts and:

```text
error: [Errno 1] Operation not permitted
```

The top-level run therefore ended with
`failure=vcpu-affinity-pinning`. No Spindle cache population, loader launch,
`namei_ext` condition, FUSE condition, correctness oracle, or timing sample
executed.

## Interpretation

This is an execution-environment permission failure, not a workload or
mechanism result. It provides no RQ1 or RQ2 evidence and does not count as a
real end-to-end workload preflight attempt. The source tree and kernel tree
were clean, host CPUs 4--7 used the `performance` governor, turbo was disabled,
and the full host gate passed before KVM launch.

## Next step

Run the same unchanged preflight through an execution context that permits
QEMU vCPU affinity changes, under a new result root. Preserve the paired
two-boot protocol, workload oracle, warmup and sample counts, and all analysis
rules unchanged.

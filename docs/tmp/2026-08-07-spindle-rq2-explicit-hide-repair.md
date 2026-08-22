# Spindle RQ2 Explicit-Hide Repair

## Motivation

The first guest in preflight04 passed the source, permission, identity, target
engagement, warmup, and measured loader checks, but a post-withdrawal `fstatat`
still found `libtest10.so`. The namei_ext arm had deleted its redirect rule.
The Spindle policy treats a missing rule as `PASS`, so VFS lookup returned the
existing lower source file. This was not feature-equivalent to the FUSE arm,
which makes the name absent from lookup.

## Implementation

`spindle_staging_rules` now uses the same compact convention as other project
policies: no map entry means `PASS`, a present zero value means `HIDE`, and a
valid nonzero target ID means `SELECT_TARGET` during lookup. The policy applies
the zero-valued rule to lookup and directory-enumeration events. Positive
target rules continue to select only during lookup and pass during directory
enumeration, preserving the existing Spindle view.

The RQ2 runner replaces the `libtest10.so` target rule with a zero-valued hide
rule. It records the lookup-hide counter before and after the withdrawal
oracles. A successful row now requires all of the following:

- the non-root `fstatat` observes `ENOENT`;
- the Spindle loader fails with the exact missing-library diagnostic;
- the selected-target hit counter does not advance;
- the lookup-hide counter advances.

Post-withdrawal counters are collected even when a behavioral oracle fails, so
a failed run does not report an uncollected zero as if the counter decreased.
The analyzer and the per-boot Make gate both require the new engagement field.

## Validation

The modified BPF policy compiles with clang. The statically linked RQ2 runner
compiles with `-Werror`. Thirteen analyzer tests pass, including a regression
that rejects a namei_ext withdrawal with no hide-counter activity. `make
spindle-staging-rq2-host-gate` passes. A fresh paired modified-kernel KVM
preflight remained required. Preflight06 subsequently passed the direct
`fstatat(ENOENT)`, exact loader failure, unchanged selected-target hits, and
positive hide-counter delta in the real attach path.

## Scope

This repair implements the withdrawal transition already required by the
approved plan. It does not change the workload, FUSE baseline, timing metric,
sample count, or acceptance rule.

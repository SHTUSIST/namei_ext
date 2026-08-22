#!/usr/bin/env python3

import argparse
import csv
import errno
import json
import math
import random
import statistics
from pathlib import Path


CONDITIONS = ("namei_ext", "fuse")


def read_jsonl(path):
    rows = []
    with Path(path).open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
    return rows


def percentile(values, probability):
    ordered = sorted(values)
    position = (len(ordered) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def bootstrap_geomean_ci(log_ratios, seed, iterations=10000):
    random_source = random.Random(seed)
    count = len(log_ratios)
    estimates = []
    for _ in range(iterations):
        sample = [log_ratios[random_source.randrange(count)] for _ in range(count)]
        estimates.append(math.exp(statistics.fmean(sample)))
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def analyze(observations, run, launches, seed):
    matrix = run.get("matrix", {})
    repetitions = matrix.get("repetitions")
    samples_per_boot = matrix.get("measured_samples_per_boot")
    if not isinstance(repetitions, int) or repetitions < 1:
        raise ValueError("run matrix has no positive repetition count")
    if not isinstance(samples_per_boot, int) or samples_per_boot < 1:
        raise ValueError("run matrix has no positive measured sample count")
    if matrix.get("conditions") != list(CONDITIONS):
        raise ValueError("run matrix conditions do not match the frozen protocol")
    if any(row.get("pass") is not True for row in observations):
        raise ValueError("correctness or engagement oracle failed")

    expected_launches = repetitions * len(CONDITIONS)
    if len(launches) != expected_launches:
        raise ValueError("launch-order row count does not match the run matrix")
    expected_order = []
    for repetition in range(1, repetitions + 1):
        order = CONDITIONS if repetition % 2 else tuple(reversed(CONDITIONS))
        expected_order.extend((repetition, condition) for condition in order)
    observed_order = [
        (row.get("repetition"), row.get("condition")) for row in launches
    ]
    if observed_order != expected_order:
        raise ValueError("launch order does not match alternating paired boots")

    lower_filesystems = [
        row
        for row in observations
        if row.get("event") == "spindle-staging-rq2-lower-filesystem"
    ]
    lower_keys = {
        (row.get("repetition"), row.get("condition"))
        for row in lower_filesystems
        if row.get("runtime_fstype") == "tmpfs"
    }
    expected_condition_keys = {
        (repetition, condition)
        for repetition in range(1, repetitions + 1)
        for condition in CONDITIONS
    }
    if (
        len(lower_filesystems) != expected_launches
        or lower_keys != expected_condition_keys
    ):
        raise ValueError("every condition must use the guest-local tmpfs runtime")

    permissions = [
        row
        for row in observations
        if row.get("event") == "spindle-staging-rq2-permission"
    ]
    permission_keys = {
        (row.get("repetition"), row.get("condition")) for row in permissions
    }
    if (
        len(permissions) != expected_launches
        or permission_keys != expected_condition_keys
        or any(
            row.get("observed_errno") != errno.EACCES
            or row.get("restore_errno") != 0
            for row in permissions
        )
    ):
        raise ValueError("every condition must pass the EACCES permission oracle")

    withdrawal_lookups = [
        row
        for row in observations
        if row.get("event") == "spindle-staging-rq2-withdrawal-lookup"
    ]
    withdrawal_lookup_keys = {
        (row.get("repetition"), row.get("condition"))
        for row in withdrawal_lookups
    }
    if (
        len(withdrawal_lookups) != expected_launches
        or withdrawal_lookup_keys != expected_condition_keys
        or any(
            row.get("operation") != "fstatat"
            or row.get("observed_errno") != errno.ENOENT
            or row.get("expected_errno") != errno.ENOENT
            for row in withdrawal_lookups
        )
    ):
        raise ValueError("every withdrawn pathname must fail fstatat with ENOENT")

    withdrawals = [
        row
        for row in observations
        if row.get("event") == "spindle-staging-rq2-withdrawal"
    ]
    withdrawal_keys = {
        (row.get("repetition"), row.get("condition")) for row in withdrawals
    }
    if (
        len(withdrawals) != expected_launches
        or withdrawal_keys != expected_condition_keys
        or any(
            row.get("exit_status") == 0
            or row.get("runner_errno") != 0
            or row.get("expected_diagnostic") is not True
            for row in withdrawals
        )
    ):
        raise ValueError("every condition must pass the withdrawn-loader oracle")

    withdrawal_windows = [
        row
        for row in observations
        if row.get("event") == "spindle-staging-rq2-withdrawal-window"
    ]
    withdrawal_window_keys = {
        (row.get("repetition"), row.get("condition"))
        for row in withdrawal_windows
    }
    if (
        len(withdrawal_windows) != expected_launches
        or withdrawal_window_keys != expected_condition_keys
        or any(
            not isinstance(row.get("before"), int)
            or row.get("after") != row.get("before")
            or (
                row.get("condition") == "namei_ext"
                and (
                    not isinstance(row.get("hide_before"), int)
                    or not isinstance(row.get("hide_after"), int)
                    or row["hide_after"] <= row["hide_before"]
                )
            )
            for row in withdrawal_windows
        )
    ):
        raise ValueError(
            "withdrawal must hide the name without engaging the selected backing object"
        )

    invalidations = [
        row
        for row in observations
        if row.get("event") == "spindle-staging-rq2-fuse-invalidation"
    ]
    expected_invalidation_keys = {
        (repetition, phase)
        for repetition in range(1, repetitions + 1)
        for phase in ("mode_zero", "mode_restore", "withdraw")
    }
    invalidation_keys = {
        (row.get("repetition"), row.get("phase")) for row in invalidations
    }
    if (
        len(invalidations) != repetitions * 3
        or invalidation_keys != expected_invalidation_keys
        or any(
            row.get("condition") != "fuse"
            or row.get("status") != 0
            or row.get("inode_status") != 0
            or row.get("entry_status") not in (0, -errno.ENOENT)
            or row.get("epoch_status") != 0
            or row.get("epoch_attempted")
            != (row.get("entry_status") == -errno.ENOENT)
            for row in invalidations
        )
    ):
        raise ValueError(
            "FUSE invalidation or required ENOENT epoch fallback failed"
        )

    lifecycle = {}
    for row in observations:
        if row.get("event") != "spindle-staging-rq2-lifecycle":
            continue
        key = (row.get("repetition"), row.get("condition"), row.get("phase"))
        duration_ns = row.get("duration_ns")
        if (
            key[0] not in range(1, repetitions + 1)
            or key[1] not in CONDITIONS
            or key[2] not in ("setup", "teardown")
            or not isinstance(duration_ns, int)
            or duration_ns <= 0
            or key in lifecycle
        ):
            raise ValueError("invalid or duplicate lifecycle measurement")
        lifecycle[key] = duration_ns
    expected_lifecycle_keys = {
        (repetition, condition, phase)
        for repetition in range(1, repetitions + 1)
        for condition in CONDITIONS
        for phase in ("setup", "teardown")
    }
    if set(lifecycle) != expected_lifecycle_keys:
        raise ValueError("setup and teardown measurements are required per boot")

    measured = {}
    for row in observations:
        if row.get("event") != "spindle-staging-rq2-sample":
            continue
        if row.get("phase") != "measured":
            continue
        key = (row.get("repetition"), row.get("condition"))
        duration_ns = row.get("duration_ns")
        iteration = row.get("iteration")
        resource_fields = (
            "user_cpu_ns",
            "system_cpu_ns",
            "minor_faults",
            "major_faults",
            "voluntary_context_switches",
            "involuntary_context_switches",
        )
        if (
            key[0] not in range(1, repetitions + 1)
            or key[1] not in CONDITIONS
            or not isinstance(duration_ns, int)
            or duration_ns <= 0
            or not isinstance(iteration, int)
            or iteration < 1
            or any(
                not isinstance(row.get(field), int) or row[field] < 0
                for field in resource_fields
            )
        ):
            raise ValueError("invalid measured sample")
        bucket = measured.setdefault(key, {})
        if iteration in bucket:
            raise ValueError(f"duplicate iteration {iteration} for {key}")
        bucket[iteration] = row

    fuse_resources = {}
    for row in observations:
        if row.get("event") != "spindle-staging-rq2-fuse-resource":
            continue
        repetition = row.get("repetition")
        if (
            repetition not in range(1, repetitions + 1)
            or row.get("condition") != "fuse"
            or repetition in fuse_resources
            or not isinstance(row.get("cpu_runtime_ns"), int)
            or row["cpu_runtime_ns"] <= 0
        ):
            raise ValueError("invalid or duplicate FUSE resource window")
        fuse_resources[repetition] = row
    if len(fuse_resources) != repetitions:
        raise ValueError("one FUSE resource window is required per FUSE boot")

    namei_windows = {}
    fuse_callback_windows = {}
    for row in observations:
        repetition = row.get("repetition")
        if row.get("event") == "spindle-staging-rq2-namei-window":
            if (
                repetition in namei_windows
                or repetition not in range(1, repetitions + 1)
                or row.get("condition") != "namei_ext"
                or not isinstance(row.get("select_delta"), int)
                or row["select_delta"] <= 0
            ):
                raise ValueError("invalid or duplicate namei_ext counter window")
            namei_windows[repetition] = row
        if (
            row.get("event") == "spindle-staging-rq2-fuse-counter"
            and row.get("counter") == "total"
        ):
            if (
                repetition in fuse_callback_windows
                or repetition not in range(1, repetitions + 1)
                or row.get("condition") != "fuse"
                or not isinstance(row.get("delta"), int)
                or row["delta"] <= 0
            ):
                raise ValueError("invalid or duplicate FUSE callback window")
            fuse_callback_windows[repetition] = row
    expected_repetitions = set(range(1, repetitions + 1))
    if (
        set(namei_windows) != expected_repetitions
        or set(fuse_callback_windows) != expected_repetitions
    ):
        raise ValueError("one mechanism counter window is required per boot")

    boot_rows = []
    pair_rows = []
    log_ratios = []
    total_cpu_log_ratios = []
    pooled = {condition: [] for condition in CONDITIONS}
    for repetition in range(1, repetitions + 1):
        medians = {}
        client_cpu = {}
        for condition in CONDITIONS:
            values_by_iteration = measured.get((repetition, condition), {})
            if len(values_by_iteration) != samples_per_boot:
                raise ValueError(
                    f"block {repetition} {condition} has "
                    f"{len(values_by_iteration)} samples; "
                    f"expected {samples_per_boot}"
                )
            expected_iterations = set(range(1, samples_per_boot + 1))
            if set(values_by_iteration) != expected_iterations:
                raise ValueError(
                    f"block {repetition} {condition} iterations are not 1.."
                    f"{samples_per_boot}"
                )
            values = [
                values_by_iteration[iteration]
                for iteration in range(1, samples_per_boot + 1)
            ]
            durations = [row["duration_ns"] for row in values]
            median_ns = statistics.median(durations)
            client_cpu_ns = sum(
                row["user_cpu_ns"] + row["system_cpu_ns"] for row in values
            )
            medians[condition] = median_ns
            client_cpu[condition] = client_cpu_ns
            pooled[condition].extend(values)
            boot_rows.append(
                {
                    "repetition": repetition,
                    "condition": condition,
                    "samples": len(values),
                    "median_ns": median_ns,
                    "p95_ns": percentile(durations, 0.95),
                    "minimum_ns": min(durations),
                    "maximum_ns": max(durations),
                    "client_cpu_ns": client_cpu_ns,
                    "major_faults": sum(row["major_faults"] for row in values),
                    "minor_faults": sum(row["minor_faults"] for row in values),
                    "setup_ns": lifecycle[(repetition, condition, "setup")],
                    "teardown_ns": lifecycle[(
                        repetition,
                        condition,
                        "teardown",
                    )],
                }
            )
        ratio = medians["fuse"] / medians["namei_ext"]
        namei_total_cpu = client_cpu["namei_ext"]
        fuse_total_cpu = (
            client_cpu["fuse"] + fuse_resources[repetition]["cpu_runtime_ns"]
        )
        total_cpu_ratio = fuse_total_cpu / namei_total_cpu
        log_ratios.append(math.log(ratio))
        total_cpu_log_ratios.append(math.log(total_cpu_ratio))
        pair_rows.append(
            {
                "repetition": repetition,
                "namei_ext_median_ns": medians["namei_ext"],
                "fuse_median_ns": medians["fuse"],
                "fuse_over_namei_ext": ratio,
                "namei_ext_total_cpu_ns": namei_total_cpu,
                "fuse_total_cpu_ns": fuse_total_cpu,
                "fuse_over_namei_ext_total_cpu": total_cpu_ratio,
            }
        )

    point = math.exp(statistics.fmean(log_ratios))
    lower, upper = bootstrap_geomean_ci(log_ratios, seed)
    if lower > 1.0:
        verdict = "supported"
    elif upper < 1.0:
        verdict = "contradicted"
    else:
        verdict = "inconclusive"

    total_cpu_point = math.exp(statistics.fmean(total_cpu_log_ratios))
    total_cpu_lower, total_cpu_upper = bootstrap_geomean_ci(
        total_cpu_log_ratios, seed + 1
    )
    condition_rows = {}
    for condition in CONDITIONS:
        rows = pooled[condition]
        durations = [row["duration_ns"] for row in rows]
        condition_rows[condition] = {
            "samples": len(rows),
            "p50_ns": percentile(durations, 0.50),
            "p95_ns": percentile(durations, 0.95),
            "mean_client_cpu_ns": statistics.fmean(
                row["user_cpu_ns"] + row["system_cpu_ns"] for row in rows
            ),
            "mean_major_faults": statistics.fmean(
                row["major_faults"] for row in rows
            ),
            "mean_minor_faults": statistics.fmean(
                row["minor_faults"] for row in rows
            ),
            "median_setup_ns": statistics.median(
                lifecycle[(repetition, condition, "setup")]
                for repetition in range(1, repetitions + 1)
            ),
            "median_teardown_ns": statistics.median(
                lifecycle[(repetition, condition, "teardown")]
                for repetition in range(1, repetitions + 1)
            ),
        }

    return {
        "schema": "namei_ext.spindle_staging_rq2.summary.v2",
        "unit": "fresh-boot median of measured loader launches",
        "repetitions": repetitions,
        "measured_samples_per_boot": samples_per_boot,
        "boot_rows": boot_rows,
        "pair_rows": pair_rows,
        "primary": {
            "metric": "fuse_over_namei_ext_loader_latency",
            "geometric_mean_ratio": point,
            "paired_bootstrap_95_ci": [lower, upper],
            "bootstrap_iterations": 10000,
            "seed": seed,
            "verdict": verdict,
        },
        "total_cpu": {
            "metric": "fuse_client_plus_daemon_over_namei_ext_client_cpu",
            "geometric_mean_ratio": total_cpu_point,
            "paired_bootstrap_95_ci": [total_cpu_lower, total_cpu_upper],
            "bootstrap_iterations": 10000,
            "seed": seed + 1,
        },
        "conditions": condition_rows,
        "mechanism_work": {
            "namei_ext_selects_per_launch": statistics.fmean(
                row["select_delta"] / samples_per_boot
                for row in namei_windows.values()
            ),
            "fuse_callbacks_per_launch": statistics.fmean(
                row["delta"] / samples_per_boot
                for row in fuse_callback_windows.values()
            ),
        },
        "fuse_daemon": {
            "median_cpu_runtime_ns": statistics.median(
                row["cpu_runtime_ns"] for row in fuse_resources.values()
            ),
            "median_voluntary_context_switches": statistics.median(
                row["voluntary_context_switches"]
                for row in fuse_resources.values()
            ),
            "median_involuntary_context_switches": statistics.median(
                row["involuntary_context_switches"]
                for row in fuse_resources.values()
            ),
        },
        "scope": (
            "One-node Spindle source-derived final-object selection; no claim "
            "about Spindle distribution, cache population scalability, or MPI scale."
        ),
    }


def write_outputs(summary, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    with (output / "summary.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(
            target,
            fieldnames=(
                "repetition",
                "namei_ext_median_ns",
                "fuse_median_ns",
                "fuse_over_namei_ext",
                "namei_ext_total_cpu_ns",
                "fuse_total_cpu_ns",
                "fuse_over_namei_ext_total_cpu",
            ),
        )
        writer.writeheader()
        writer.writerows(summary["pair_rows"])
    primary = summary["primary"]
    lower, upper = primary["paired_bootstrap_95_ci"]
    total_cpu = summary["total_cpu"]
    cpu_lower, cpu_upper = total_cpu["paired_bootstrap_95_ci"]
    namei = summary["conditions"]["namei_ext"]
    fuse = summary["conditions"]["fuse"]
    report = [
        "# Spindle Final-Object Selection: namei_ext vs FUSE",
        "",
        f"Fresh boot pairs: {summary['repetitions']}",
        f"Measured loader launches per boot: {summary['measured_samples_per_boot']}",
        (
            "FUSE/namei_ext geometric-mean latency ratio: "
            f"{primary['geometric_mean_ratio']:.3f}x "
            f"(paired-bootstrap 95% CI {lower:.3f}--{upper:.3f})"
        ),
        f"Hypothesis verdict: {primary['verdict']}",
        (
            "FUSE/namei_ext total CPU ratio: "
            f"{total_cpu['geometric_mean_ratio']:.3f}x "
            f"(paired-bootstrap 95% CI {cpu_lower:.3f}--{cpu_upper:.3f})"
        ),
        (
            "Pooled p50/p95 loader latency: "
            f"namei_ext {namei['p50_ns'] / 1e6:.3f}/{namei['p95_ns'] / 1e6:.3f} ms; "
            f"FUSE {fuse['p50_ns'] / 1e6:.3f}/{fuse['p95_ns'] / 1e6:.3f} ms"
        ),
        (
            "Mean major faults per launch: "
            f"namei_ext {namei['mean_major_faults']:.3f}; "
            f"FUSE {fuse['mean_major_faults']:.3f}"
        ),
        (
            "Median setup/teardown: "
            f"namei_ext {namei['median_setup_ns'] / 1e6:.3f}/"
            f"{namei['median_teardown_ns'] / 1e6:.3f} ms; "
            f"FUSE {fuse['median_setup_ns'] / 1e6:.3f}/"
            f"{fuse['median_teardown_ns'] / 1e6:.3f} ms"
        ),
        (
            "Mechanism work per measured launch: "
            f"{summary['mechanism_work']['namei_ext_selects_per_launch']:.1f} "
            "namei_ext selections; "
            f"{summary['mechanism_work']['fuse_callbacks_per_launch']:.1f} "
            "FUSE callbacks"
        ),
        (
            "Median FUSE daemon CPU time in the matched measurement window: "
            f"{summary['fuse_daemon']['median_cpu_runtime_ns'] / 1e6:.3f} ms"
        ),
        "",
        f"Scope: {summary['scope']}",
    ]
    (output / "report.md").write_text("\n".join(report) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--launch-order", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--seed", required=True, type=int)
    arguments = parser.parse_args()

    observations = read_jsonl(arguments.input)
    launches = read_jsonl(arguments.launch_order)
    with Path(arguments.run).open(encoding="utf-8") as source:
        run = json.load(source)
    summary = analyze(observations, run, launches, arguments.seed)
    write_outputs(summary, arguments.output)


if __name__ == "__main__":
    main()

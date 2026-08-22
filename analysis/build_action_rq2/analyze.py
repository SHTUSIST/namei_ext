#!/usr/bin/env python3

import argparse
import csv
import json
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


CONDITIONS = ("namei_ext", "sandboxfs")
METRICS = ("action_ns", "setup_ns", "lifecycle_ns")
METRIC_LABELS = {
    "action_ns": "Action time",
    "setup_ns": "View setup time",
    "lifecycle_ns": "Lifecycle time",
}
SAMPLE_EVENT = "build-action-rq2-sample"
SUMMARY_EVENT = "build-action-rq2-summary"
COUNTER_EVENT = "build-action-rq2-policy-counter"
CAPACITY_EVENT = "build-action-rq2-capacity"
FAILURE_EVENT = "build-action-rq2-failure"
ALLOWED_EVENTS = {
    SAMPLE_EVENT,
    SUMMARY_EVENT,
    COUNTER_EVENT,
    CAPACITY_EVENT,
    FAILURE_EVENT,
}
POLICY_COUNTERS = tuple(range(9))
REQUIRED_POSITIVE_COUNTERS = tuple(range(8))
BOOTSTRAP_SAMPLES = 10000


def percentile(sorted_values, probability):
    position = (len(sorted_values) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]
    weight = position - lower
    return (sorted_values[lower] * (1 - weight) +
            sorted_values[upper] * weight)


def bootstrap_median_ci(values, rng):
    if not values:
        raise ValueError("cannot bootstrap an empty sample")
    estimates = []
    for _ in range(BOOTSTRAP_SAMPLES):
        sample = [values[rng.randrange(len(values))]
                  for _ in range(len(values))]
        estimates.append(statistics.median(sample))
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def load_json(path):
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def load_rows(path):
    rows = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            try:
                row = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid JSON: {error}") from error
            if row.get("event") not in ALLOWED_EVENTS:
                raise ValueError(
                    f"{path}:{line_number}: unexpected event")
            if row.get("pass") is not True:
                raise ValueError(
                    f"{path}:{line_number}: failed or ungraded observation")
            rows.append(row)
    return rows


def load_plan(path):
    run = load_json(path)
    if run.get("schema") != "namei_ext.run.v2" or \
            run.get("protocol_schema") != \
            "namei_ext.build_action_rq2.protocol.v1" or \
            run.get("suite") != "build-action-rq2" or \
            run.get("layout") != "paired-boot-matrix" or \
            run.get("status") != "completed" or \
            run.get("failed_at") is not None:
        raise ValueError("run is not a completed Build Action RQ2 matrix")
    matrix = run.get("matrix", {})
    if tuple(matrix.get("conditions", ())) != CONDITIONS:
        raise ValueError("unexpected condition matrix")
    repetitions = matrix.get("repetitions")
    samples = matrix.get("samples_per_scale")
    scales = matrix.get("scales")
    primary_scale = matrix.get("primary_scale")
    capacity = matrix.get("capacity_probe")
    if type(repetitions) is not int or repetitions < 1:
        raise ValueError("invalid repetition count")
    if type(samples) is not int or samples < 1:
        raise ValueError("invalid sample count")
    if not isinstance(scales, list) or not scales or \
            any(type(scale) is not int or scale < 1 for scale in scales) or \
            len(set(scales)) != len(scales):
        raise ValueError("invalid scale matrix")
    if primary_scale not in scales:
        raise ValueError("primary scale is absent from the matrix")
    if type(capacity) is not int or capacity < 0:
        raise ValueError("invalid capacity probe")
    if matrix.get("order") != "alternating" or \
            matrix.get("scale_order") != "rotating":
        raise ValueError("unexpected ordering protocol")
    if type(matrix.get("kvm_cpus")) is not int or \
            matrix.get("kvm_cpus") < 1 or \
            not matrix.get("host_cpu_pin"):
        raise ValueError("missing CPU protocol")
    return {
        "repetitions": repetitions,
        "samples": samples,
        "scales": tuple(scales),
        "primary_scale": primary_scale,
        "capacity": capacity,
    }


def load_launch_order(path, repetitions):
    rows = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid JSON: {error}") from error
    if len(rows) != 2 * repetitions:
        raise ValueError("launch-order row count does not match matrix")
    first_conditions = {}
    for index, row in enumerate(rows):
        repetition = index // 2 + 1
        expected = CONDITIONS if repetition % 2 else tuple(reversed(CONDITIONS))
        if row.get("schema") != \
                "namei_ext.build_action_rq2.launch_order.v1" or \
                row.get("order_index") != index + 1 or \
                row.get("repetition") != repetition or \
                row.get("condition") != expected[index % 2] or \
                not row.get("host_started_at") or \
                not row.get("host_completed_at"):
            raise ValueError(f"invalid launch-order row {index + 1}")
        if index % 2 == 0:
            first_conditions[repetition] = row["condition"]
    return first_conditions


def validate_identity(row, plan):
    condition = row.get("condition")
    repetition = row.get("repetition")
    if condition not in CONDITIONS or type(repetition) is not int or \
            repetition not in range(1, plan["repetitions"] + 1):
        raise ValueError("observation has an invalid boot identity")
    return repetition, condition


def validate_sample(row, plan):
    integer_fields = (
        "scale",
        "sample",
        "order_index",
        "setup_ns",
        "action_ns",
        "lifecycle_ns",
        "sandboxfs_user_ticks",
        "sandboxfs_system_ticks",
        "sandboxfs_voluntary_context_switches",
        "sandboxfs_involuntary_context_switches",
        "sandboxfs_vm_hwm_kb",
        "output_bytes_a",
        "output_bytes_b",
    )
    if any(type(row.get(field)) is not int for field in integer_fields):
        raise ValueError("sample has a missing integer field")
    if row["scale"] not in plan["scales"] or \
            row["sample"] not in range(1, plan["samples"] + 1) or \
            row["order_index"] < 1 or \
            any(row[field] <= 0
                for field in ("setup_ns", "action_ns", "lifecycle_ns")) or \
            any(row[field] < 0 for field in integer_fields[6:]) or \
            row["output_bytes_a"] == 0 or row["output_bytes_b"] == 0:
        raise ValueError("sample has an invalid measurement")
    if row.get("actions") != 2 or row.get("concurrent") is not True or \
            row.get("output_exact") is not True or \
            row.get("unknown_hidden") is not True or \
            row.get("undeclared_hidden") is not True or \
            row.get("lower_objects_unchanged") is not True:
        raise ValueError("sample correctness oracle is incomplete")


def validate(rows, plan):
    samples_by_boot = defaultdict(list)
    summaries = {}
    counters = defaultdict(dict)
    capacities = {}
    failures = 0

    for row in rows:
        repetition, condition = validate_identity(row, plan)
        event = row["event"]
        key = (repetition, condition)
        if event == FAILURE_EVENT:
            failures += 1
        elif event == SAMPLE_EVENT:
            validate_sample(row, plan)
            samples_by_boot[key].append(row)
        elif event == SUMMARY_EVENT:
            if key in summaries:
                raise ValueError("duplicate boot summary")
            summaries[key] = row
        elif event == COUNTER_EVENT:
            counter = row.get("counter")
            value = row.get("value")
            if condition != "namei_ext" or counter not in POLICY_COUNTERS or \
                    type(value) is not int or value < 0 or \
                    counter in counters[repetition]:
                raise ValueError("invalid policy counter")
            counters[repetition][counter] = value
        elif event == CAPACITY_EVENT:
            if condition != "namei_ext" or repetition in capacities:
                raise ValueError("invalid capacity probe identity")
            capacities[repetition] = row

    if failures:
        raise ValueError("failure observations are present")
    indexed = {}
    expected_per_boot = len(plan["scales"]) * plan["samples"]
    for repetition in range(1, plan["repetitions"] + 1):
        offset = (repetition - 1) % len(plan["scales"])
        scale_order = (
            plan["scales"][offset:] + plan["scales"][:offset]
        )
        expected_order = [
            (scale, sample)
            for scale in scale_order
            for sample in range(1, plan["samples"] + 1)
        ]
        for condition in CONDITIONS:
            key = (repetition, condition)
            boot_samples = samples_by_boot[key]
            if len(boot_samples) != expected_per_boot:
                raise ValueError(f"wrong sample count for {key}")
            ordered = sorted(boot_samples, key=lambda row: row["order_index"])
            if [row["order_index"] for row in ordered] != \
                    list(range(1, expected_per_boot + 1)) or \
                    [(row["scale"], row["sample"]) for row in ordered] != \
                    expected_order:
                raise ValueError(f"invalid sample order for {key}")
            for scale in plan["scales"]:
                cell = [row for row in boot_samples
                        if row["scale"] == scale]
                if sorted(row["sample"] for row in cell) != \
                        list(range(1, plan["samples"] + 1)):
                    raise ValueError(f"incomplete sample cell for {key}")
                for metric in METRICS:
                    indexed[(repetition, condition, scale, metric)] = [
                        row[metric] for row in
                        sorted(cell, key=lambda row: row["sample"])
                    ]
            summary = summaries.get(key)
            if not summary or summary.get("scales") != len(plan["scales"]) or \
                    summary.get("samples_per_scale") != plan["samples"] or \
                    summary.get("completed_samples") != expected_per_boot:
                raise ValueError(f"invalid boot summary for {key}")

        observed_counters = counters.get(repetition, {})
        if tuple(sorted(observed_counters)) != POLICY_COUNTERS or \
                any(observed_counters[counter] <= 0
                    for counter in REQUIRED_POSITIVE_COUNTERS):
            raise ValueError(f"incomplete policy counters for {repetition}")

    if plan["capacity"]:
        if len(capacities) != plan["repetitions"]:
            raise ValueError("capacity probe count does not match matrix")
        for repetition, row in capacities.items():
            expected = plan["capacity"]
            if row.get("requested") != expected or \
                    row.get("inserted") != expected or \
                    row.get("removed") != expected or \
                    row.get("remaining") != 0:
                raise ValueError(
                    f"capacity probe failed for repetition {repetition}")
    elif capacities:
        raise ValueError("unexpected capacity probe in formal matrix")

    correctness = {
        "boots_expected": 2 * plan["repetitions"],
        "boots_observed": len(summaries),
        "samples_expected": 2 * plan["repetitions"] * expected_per_boot,
        "samples_observed": sum(len(value)
                                for value in samples_by_boot.values()),
        "failed_observations": failures,
        "policy_counter_boots": len(counters),
        "capacity_probe_boots": len(capacities),
    }
    return indexed, counters, correctness


def summarize(indexed, plan, seed, first_conditions):
    rng = random.Random(seed)
    summaries = []
    for scale in plan["scales"]:
        for metric in METRICS:
            per_condition = {}
            for condition in CONDITIONS:
                boot_medians = [
                    statistics.median(
                        indexed[(repetition, condition, scale, metric)])
                    for repetition in range(1, plan["repetitions"] + 1)
                ]
                low, high = bootstrap_median_ci(boot_medians, rng)
                per_condition[condition] = {
                    "per_boot_medians_ns": boot_medians,
                    "median_ns": statistics.median(boot_medians),
                    "ci_low_ns": low,
                    "ci_high_ns": high,
                }
            ratios = [
                per_condition["sandboxfs"]["per_boot_medians_ns"][index] /
                per_condition["namei_ext"]["per_boot_medians_ns"][index]
                for index in range(plan["repetitions"])
            ]
            low, high = bootstrap_median_ci(ratios, rng)
            first_namei = [
                ratio for repetition, ratio in enumerate(ratios, 1)
                if first_conditions[repetition] == "namei_ext"
            ]
            first_sandboxfs = [
                ratio for repetition, ratio in enumerate(ratios, 1)
                if first_conditions[repetition] == "sandboxfs"
            ]
            summaries.append({
                "scale": scale,
                "metric": metric,
                "conditions": per_condition,
                "paired_ratios": ratios,
                "ratio_median": statistics.median(ratios),
                "ratio_ci_low": low,
                "ratio_ci_high": high,
                "order_diagnostic": {
                    "namei_ext_first_ratio_median":
                        statistics.median(first_namei)
                        if first_namei else None,
                    "sandboxfs_first_ratio_median":
                        statistics.median(first_sandboxfs)
                        if first_sandboxfs else None,
                },
            })
    return summaries


def classify(summaries, primary_scale):
    primary = next(
        row for row in summaries
        if row["scale"] == primary_scale and row["metric"] == "action_ns"
    )
    if primary["ratio_ci_low"] > 1:
        tested = "supported"
    elif primary["ratio_ci_high"] <= 1:
        tested = "contradicted"
    else:
        tested = "inconclusive"
    return {
        "tested_hypothesis": tested,
        "primary_scale": primary_scale,
        "metric": "action_ns",
        "ratio": "sandboxfs/namei_ext",
        "ratio_median": primary["ratio_median"],
        "ci_low": primary["ratio_ci_low"],
        "ci_high": primary["ratio_ci_high"],
        "correctness_gated": True,
    }


def write_csv(path, summaries):
    with path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow([
            "scale", "metric", "namei_ext_median_ns",
            "sandboxfs_median_ns", "sandboxfs_over_namei_ext_median",
            "ratio_ci_low", "ratio_ci_high",
        ])
        for row in summaries:
            writer.writerow([
                row["scale"],
                row["metric"],
                row["conditions"]["namei_ext"]["median_ns"],
                row["conditions"]["sandboxfs"]["median_ns"],
                row["ratio_median"],
                row["ratio_ci_low"],
                row["ratio_ci_high"],
            ])


def write_report(path, summaries, correctness, verdict, plan, seed):
    lines = [
        "# Build Action RQ2 Report",
        "",
        "## Correctness Gate",
        "",
        f"- Boots: {correctness['boots_observed']}/"
        f"{correctness['boots_expected']}",
        f"- Lifecycle samples: {correctness['samples_observed']}/"
        f"{correctness['samples_expected']}",
        f"- Failed observations: {correctness['failed_observations']}",
        f"- Policy-counter boots: {correctness['policy_counter_boots']}",
        "",
        "## Tested Hypothesis",
        "",
        f"- Verdict: **{verdict['tested_hypothesis']}**",
        f"- Primary cell: {verdict['primary_scale']} declared inputs",
        "- Primary metric: barrier release until both actions finish",
        f"- Median paired sandboxfs/namei_ext ratio: "
        f"{verdict['ratio_median']:.3f}x",
        f"- Bootstrap 95% CI: [{verdict['ci_low']:.3f}, "
        f"{verdict['ci_high']:.3f}]",
        "",
        "## Paired Results",
        "",
        "| Inputs | Metric | namei_ext median (ms) | "
        "sandboxfs median (ms) | Paired ratio | 95% CI |",
        "|---:|---|---:|---:|---:|---:|",
    ]
    for row in summaries:
        lines.append(
            f"| {row['scale']} | {METRIC_LABELS[row['metric']]} | "
            f"{row['conditions']['namei_ext']['median_ns'] / 1e6:.3f} | "
            f"{row['conditions']['sandboxfs']['median_ns'] / 1e6:.3f} | "
            f"{row['ratio_median']:.3f}x | "
            f"[{row['ratio_ci_low']:.3f}, "
            f"{row['ratio_ci_high']:.3f}] |"
        )
    lines.extend([
        "",
        "## Protocol",
        "",
        f"- Paired blocks: {plan['repetitions']}",
        f"- Samples per scale and boot: {plan['samples']}",
        f"- Scales: {', '.join(str(scale) for scale in plan['scales'])}",
        f"- Bootstrap resamples: {BOOTSTRAP_SAMPLES}",
        f"- Bootstrap seed: {seed}",
        "",
        "Ratios are computed within paired boot blocks after reducing each "
        "boot cell to the median. No failed or missing cell is excluded.",
        "",
    ])
    path.write_text("\n".join(lines), encoding="utf-8")


def write_plot(output, summaries, plan):
    figure, axes = plt.subplots(1, 2, figsize=(8.2, 3.2))
    for axis, metric in zip(axes, ("action_ns", "setup_ns")):
        for condition, marker in zip(CONDITIONS, ("o", "s")):
            rows = [row for row in summaries if row["metric"] == metric]
            values = [
                row["conditions"][condition]["median_ns"] / 1e6
                for row in rows
            ]
            low = [
                (row["conditions"][condition]["median_ns"] -
                 row["conditions"][condition]["ci_low_ns"]) / 1e6
                for row in rows
            ]
            high = [
                (row["conditions"][condition]["ci_high_ns"] -
                 row["conditions"][condition]["median_ns"]) / 1e6
                for row in rows
            ]
            axis.errorbar(
                plan["scales"], values, yerr=[low, high], label=condition,
                marker=marker, capsize=3, linewidth=1.5,
            )
        axis.set_xscale("log", base=2)
        axis.set_yscale("log")
        axis.set_xticks(plan["scales"], [str(scale)
                                        for scale in plan["scales"]])
        axis.set_xlabel("Declared inputs per action")
        axis.set_ylabel(f"{METRIC_LABELS[metric]} (ms)")
        axis.grid(True, which="both", alpha=0.25)
    axes[0].legend(frameon=False)
    figure.tight_layout()
    figure.savefig(output / "action-time.pdf", bbox_inches="tight")
    figure.savefig(output / "action-time.png", dpi=200, bbox_inches="tight")
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", required=True, type=Path)
    parser.add_argument("--observations", required=True, type=Path)
    parser.add_argument("--launch-order", required=True, type=Path)
    parser.add_argument("--seed", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    plan = load_plan(args.run)
    first_conditions = load_launch_order(
        args.launch_order, plan["repetitions"])
    rows = load_rows(args.observations)
    indexed, counters, correctness = validate(rows, plan)
    summaries = summarize(indexed, plan, args.seed, first_conditions)
    verdict = classify(summaries, plan["primary_scale"])

    args.output.mkdir(parents=True, exist_ok=False)
    summary = {
        "schema": "namei_ext.build_action_rq2.summary.v1",
        "plan": plan,
        "correctness": correctness,
        "policy_counters": counters,
        "results": summaries,
        "verdict": verdict,
        "bootstrap": {
            "method": "paired-boot percentile bootstrap of medians",
            "samples": BOOTSTRAP_SAMPLES,
            "seed": args.seed,
        },
    }
    (args.output / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_csv(args.output / "summary.csv", summaries)
    write_report(
        args.output / "report.md", summaries, correctness, verdict,
        plan, args.seed,
    )
    write_plot(args.output, summaries, plan)


if __name__ == "__main__":
    main()

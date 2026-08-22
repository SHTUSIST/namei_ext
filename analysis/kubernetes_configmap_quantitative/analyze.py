#!/usr/bin/env python3

import argparse
import csv
import json
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path


EVENT = "kubernetes-configmap-quantitative-lifecycle"
AUDIT_EVENT = "kubernetes-configmap-quantitative-materialization-audit"
MECHANISMS = ("atomicwriter", "namei_ext")
PRIMARY_METRIC = "wall_span_ns"
PHASES = (
    "setup_ns",
    "initial_publish_ns",
    "initial_consumer_ns",
    "update_publish_ns",
    "update_consumer_ns",
    "no_op_publish_ns",
    "no_op_consumer_ns",
    "rollback_publish_ns",
    "rollback_consumer_ns",
)
SETUP_PHASES = (
    "object_preparation_ns",
    "target_registration_ns",
    "map_population_ns",
    "consumer_cgroup_move_ns",
)
BASE_TIMINGS = (
    "wall_span_ns",
    "active_total_ns",
    "publication_only_ns",
    "consumer_only_ns",
)


def load_json(path):
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def load_jsonl(path):
    rows = []
    with path.open(encoding="utf-8") as source:
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
    if not ordered:
        raise ValueError("cannot take percentile of an empty sequence")
    position = probability * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def bootstrap_median_ci(values, seed=20260807, samples=10000):
    generator = random.Random(seed)
    draws = []
    for _ in range(samples):
        resample = [values[generator.randrange(len(values))]
                    for _ in values]
        draws.append(statistics.median(resample))
    return percentile(draws, 0.025), percentile(draws, 0.975)


def timing_snapshot(row):
    values = {name: row[name] for name in BASE_TIMINGS}
    phases = row.get("phases", {})
    if set(phases) != set(PHASES):
        raise ValueError("lifecycle phase timing set differs")
    values.update({name: phases[name] for name in PHASES})
    if row["mechanism"] == "namei_ext":
        setup = row.get("setup_phases", {})
        if (set(setup) != set(SETUP_PHASES) or
                sum(setup.values()) != phases["setup_ns"] or
                row.get("registered_targets") != 2 * (row["width"] - 1)):
            raise ValueError("namei_ext setup decomposition differs")
        values.update({name: setup[name] for name in SETUP_PHASES})
        values["attach_ns"] = row["attach_ns"]
    if any(value <= 0 for value in values.values()):
        raise ValueError("non-positive lifecycle timing present")
    return values


def materialization_snapshot(atomic_audit, proposed):
    materialization = atomic_audit.get("materialization", [])
    if len(materialization) != 4:
        raise ValueError("AtomicWriter materialization audit is incomplete")
    snapshot = {
        "atomicwriter_newly_materialized_files": sum(
            entry["newly_materialized_files"] for entry in materialization),
        "atomicwriter_newly_materialized_bytes": sum(
            entry["newly_materialized_bytes"] for entry in materialization),
        "namei_ext_lower_files": proposed["lower_files"],
        "namei_ext_lower_bytes": proposed["lower_bytes"],
        "namei_ext_logical_files": proposed["logical_files"],
        "namei_ext_prepared_regular_files": (
            proposed["lower_files"] + proposed["logical_files"]),
    }
    for suffix, entry in zip(
            ("initial", "update", "no_op", "rollback"), materialization):
        snapshot[f"atomicwriter_live_files_{suffix}"] = entry[
            "live_regular_files"]
        snapshot[f"atomicwriter_live_bytes_{suffix}"] = entry[
            "live_payload_bytes"]
        snapshot[f"namei_ext_visible_files_{suffix}"] = proposed[
            "present_per_state"]
    return snapshot


def validate(rows, run):
    matrix = run["matrix"]
    boots = int(matrix["boots"])
    pairs = int(matrix["pairs_per_scale_per_boot"])
    scales = [int(value) for value in matrix["scales"]]
    expected = boots * pairs * len(scales) * 2
    if len(rows) != expected:
        raise ValueError(f"expected {expected} lifecycle rows, found {len(rows)}")
    grouped = defaultdict(list)
    for row in rows:
        if row.get("event") != EVENT or row.get("mechanism") not in MECHANISMS:
            raise ValueError("unexpected lifecycle event or mechanism")
        if row.get("pass") is not True or row.get("cleanup_pass") is not True:
            raise ValueError("failed lifecycle row present")
        if row.get(PRIMARY_METRIC, 0) <= 0:
            raise ValueError("non-positive complete lifecycle time")
        timing_snapshot(row)
        if row.get("width") not in scales:
            raise ValueError("undeclared payload width")
        grouped[(row["boot"], row["width"], row["pair"])].append(row)
    expected_groups = boots * pairs * len(scales)
    if len(grouped) != expected_groups:
        raise ValueError("missing or duplicate boot/width/pair groups")
    for key, group in grouped.items():
        if sorted(row["mechanism"] for row in group) != list(MECHANISMS):
            raise ValueError(f"unpaired mechanisms for {key}")
        if sorted(row["order"] for row in group) != [1, 2]:
            raise ValueError(f"invalid condition order for {key}")
    return boots, pairs, scales, grouped


def summarize(rows, run, audits=None):
    boots, pairs, scales, grouped = validate(rows, run)
    audit_by_key = {}
    if audits is not None:
        for audit in audits:
            key = (audit.get("boot"), audit.get("width"), audit.get("pair"))
            if (audit.get("event") != AUDIT_EVENT or
                    audit.get("mechanism") != "atomicwriter" or
                    audit.get("pass") is not True or key in audit_by_key):
                raise ValueError("invalid or duplicate AtomicWriter audit")
            audit_by_key[key] = audit
        if set(audit_by_key) != set(grouped):
            raise ValueError("AtomicWriter audit matrix differs")
    boot_scale_logs = defaultdict(list)
    boot_scale_publication_logs = defaultdict(list)
    boot_scale_method = defaultdict(
        lambda: defaultdict(lambda: defaultdict(list)))
    boot_scale_objects = defaultdict(lambda: defaultdict(list))
    pair_rows = []
    for (boot, width, pair), group in sorted(grouped.items()):
        by_mechanism = {row["mechanism"]: row for row in group}
        atomic_row = by_mechanism["atomicwriter"]
        namei_row = by_mechanism["namei_ext"]
        atomic = atomic_row[PRIMARY_METRIC]
        namei = namei_row[PRIMARY_METRIC]
        ratio = namei / atomic
        boot_scale_logs[(boot, width)].append(math.log(ratio))
        publication_ratio = (namei_row["publication_only_ns"] /
                             atomic_row["publication_only_ns"])
        boot_scale_publication_logs[(boot, width)].append(
            math.log(publication_ratio))
        snapshots = {}
        for mechanism, row in by_mechanism.items():
            snapshots[mechanism] = timing_snapshot(row)
            for metric, value in snapshots[mechanism].items():
                boot_scale_method[(boot, width)][mechanism][metric].append(
                    value)
        pair_entry = {
            "boot": boot,
            "width": width,
            "pair": pair,
            "atomicwriter_ns": atomic,
            "namei_ext_ns": namei,
            "ratio": ratio,
            "publication_ratio": publication_ratio,
            "timing_ns": snapshots,
        }
        if audits is not None:
            objects = materialization_snapshot(
                audit_by_key[(boot, width, pair)], namei_row)
            pair_entry["materialization"] = objects
            for metric, value in objects.items():
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    boot_scale_objects[(boot, width)][metric].append(value)
        pair_rows.append(pair_entry)
    per_boot = []
    for (boot, width), logs in sorted(boot_scale_logs.items()):
        methods = boot_scale_method[(boot, width)]
        timings = {
            mechanism: {
                metric: statistics.median(values)
                for metric, values in sorted(metrics.items())
            }
            for mechanism, metrics in sorted(methods.items())
        }
        entry = {
            "boot": boot,
            "width": width,
            "pairs": len(logs),
            "median_log_ratio": statistics.median(logs),
            "ratio": math.exp(statistics.median(logs)),
            "median_log_publication_ratio": statistics.median(
                boot_scale_publication_logs[(boot, width)]),
            "publication_ratio": math.exp(statistics.median(
                boot_scale_publication_logs[(boot, width)])),
            "atomicwriter_median_ns": timings["atomicwriter"][PRIMARY_METRIC],
            "namei_ext_median_ns": timings["namei_ext"][PRIMARY_METRIC],
            "timing_median_ns": timings,
        }
        if audits is not None:
            entry["materialization_median"] = {
                metric: statistics.median(values)
                for metric, values in sorted(
                    boot_scale_objects[(boot, width)].items())
            }
        per_boot.append(entry)
    scales_summary = []
    for width in scales:
        boot_rows = [row for row in per_boot if row["width"] == width]
        logs = [row["median_log_ratio"] for row in boot_rows]
        publication_logs = [
            row["median_log_publication_ratio"] for row in boot_rows]
        center = statistics.median(logs)
        timing_median = {}
        timing_ci95 = {}
        for mechanism in MECHANISMS:
            metric_names = boot_rows[0]["timing_median_ns"][mechanism]
            timing_median[mechanism] = {}
            timing_ci95[mechanism] = {}
            for metric in metric_names:
                values = [
                    row["timing_median_ns"][mechanism][metric]
                    for row in boot_rows]
                timing_median[mechanism][metric] = statistics.median(values)
                timing_ci95[mechanism][metric] = (
                    list(bootstrap_median_ci(values)) if boots >= 20
                    else [None, None])
        entry = {
            "width": width,
            "boots": len(boot_rows),
            "registered_targets": 2 * (width - 1),
            "ratio": math.exp(center),
            "publication_ratio": math.exp(statistics.median(publication_logs)),
            "atomicwriter_median_ns": timing_median["atomicwriter"][PRIMARY_METRIC],
            "namei_ext_median_ns": timing_median["namei_ext"][PRIMARY_METRIC],
            "timing_median_ns": timing_median,
            "timing_ci95_ns": timing_ci95,
        }
        if audits is not None:
            entry["materialization_median"] = {
                metric: statistics.median(
                    row["materialization_median"][metric]
                    for row in boot_rows)
                for metric in boot_rows[0]["materialization_median"]
            }
        if boots >= 20:
            lower, upper = bootstrap_median_ci(logs)
            entry["ratio_ci95_lower"] = math.exp(lower)
            entry["ratio_ci95_upper"] = math.exp(upper)
            entry["supports_lower_cost"] = entry["ratio_ci95_upper"] < 1.0
            lower, upper = bootstrap_median_ci(publication_logs)
            entry["publication_ratio_ci95_lower"] = math.exp(lower)
            entry["publication_ratio_ci95_upper"] = math.exp(upper)
            entry["supports_lower_publication_cost"] = (
                entry["publication_ratio_ci95_upper"] < 1.0)
        else:
            entry["ratio_ci95_lower"] = None
            entry["ratio_ci95_upper"] = None
            entry["supports_lower_cost"] = None
            entry["publication_ratio_ci95_lower"] = None
            entry["publication_ratio_ci95_upper"] = None
            entry["supports_lower_publication_cost"] = None
        scales_summary.append(entry)
    primary = next(entry for entry in scales_summary
                   if entry["width"] == max(scales))
    if boots < 20:
        verdict = "preflight-complete"
    elif primary["supports_lower_cost"]:
        verdict = "supports-lower-cost-at-primary-scale"
    else:
        verdict = "does-not-support-lower-cost-at-primary-scale"
    return {
        "event": "kubernetes-configmap-quantitative-analysis",
        "boots": boots,
        "pairs_per_scale_per_boot": pairs,
        "lifecycle_rows": len(rows),
        "scales": scales_summary,
        "primary_width": max(scales),
        "primary_metric": PRIMARY_METRIC,
        "verdict": verdict,
    }, per_boot, pair_rows


def write_json(path, value):
    with path.open("w", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")


def write_jsonl(path, values):
    with path.open("w", encoding="utf-8") as output:
        for value in values:
            output.write(json.dumps(value, sort_keys=True) + "\n")


def write_csv_artifacts(analysis, summary):
    with (analysis / "scaling.csv").open(
            "w", encoding="utf-8", newline="") as output:
        fields = [
            "width", "boots", "atomicwriter_median_ns", "namei_ext_median_ns",
            "ratio", "ratio_ci95_lower", "ratio_ci95_upper",
            "publication_ratio", "publication_ratio_ci95_lower",
            "publication_ratio_ci95_upper",
        ]
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for entry in summary["scales"]:
            writer.writerow({name: entry[name] for name in fields})
    with (analysis / "decomposition.csv").open(
            "w", encoding="utf-8", newline="") as output:
        fields = [
            "width", "mechanism", *BASE_TIMINGS, *PHASES, *SETUP_PHASES,
            "attach_ns", "registered_targets",
        ]
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for entry in summary["scales"]:
            for mechanism in MECHANISMS:
                timings = entry["timing_median_ns"][mechanism]
                writer.writerow({
                    "width": entry["width"],
                    "mechanism": mechanism,
                    **{name: timings.get(name, "") for name in fields[2:-1]},
                    "registered_targets": (
                        entry["registered_targets"]
                        if mechanism == "namei_ext" else ""),
                })
    with (analysis / "materialization.csv").open(
            "w", encoding="utf-8", newline="") as output:
        fields = [
            "width", "atomicwriter_newly_materialized_files",
            "atomicwriter_newly_materialized_bytes", "namei_ext_lower_files",
            "namei_ext_lower_bytes", "namei_ext_logical_files",
            "namei_ext_prepared_regular_files",
            "atomicwriter_live_files_initial", "atomicwriter_live_files_update",
            "atomicwriter_live_files_no_op", "atomicwriter_live_files_rollback",
            "atomicwriter_live_bytes_initial", "atomicwriter_live_bytes_update",
            "atomicwriter_live_bytes_no_op", "atomicwriter_live_bytes_rollback",
            "namei_ext_visible_files_initial", "namei_ext_visible_files_update",
            "namei_ext_visible_files_no_op", "namei_ext_visible_files_rollback",
        ]
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for entry in summary["scales"]:
            objects = entry["materialization_median"]
            writer.writerow({
                "width": entry["width"],
                **{name: objects[name] for name in fields[1:]},
            })


def write_report(path, summary):
    lines = [
        "# Kubernetes ConfigMap Quantitative Result",
        "",
        f"Boots: {summary['boots']}",
        f"Lifecycle rows: {summary['lifecycle_rows']}",
        f"Primary metric: {summary['primary_metric']}",
        f"Verdict: {summary['verdict']}",
        "",
        "| Width | AtomicWriter median (ms) | namei_ext median (ms) | Ratio | 95% CI |",
        "| ---: | ---: | ---: | ---: | ---: |",
    ]
    for entry in summary["scales"]:
        if entry["ratio_ci95_lower"] is None:
            interval = "preflight only"
        else:
            interval = (f"[{entry['ratio_ci95_lower']:.3f}, "
                        f"{entry['ratio_ci95_upper']:.3f}]")
        lines.append(
            f"| {entry['width']} | "
            f"{entry['atomicwriter_median_ns'] / 1_000_000:.3f} | "
            f"{entry['namei_ext_median_ns'] / 1_000_000:.3f} | "
            f"{entry['ratio']:.3f} | {interval} |")
    lines.extend([
        "",
        "## Timing Decomposition",
        "",
        "| Width | AtomicWriter setup (ms) | namei_ext setup (ms) | "
        "AtomicWriter publication (ms) | namei_ext publication (ms) | "
        "AtomicWriter consumer (ms) | namei_ext consumer (ms) | "
        "namei_ext attach (ms) | Publication ratio |",
        "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for entry in summary["scales"]:
        atomic = entry["timing_median_ns"]["atomicwriter"]
        namei = entry["timing_median_ns"]["namei_ext"]
        lines.append(
            f"| {entry['width']} | {atomic['setup_ns'] / 1_000_000:.3f} | "
            f"{namei['setup_ns'] / 1_000_000:.3f} | "
            f"{atomic['publication_only_ns'] / 1_000_000:.3f} | "
            f"{namei['publication_only_ns'] / 1_000_000:.3f} | "
            f"{atomic['consumer_only_ns'] / 1_000_000:.3f} | "
            f"{namei['consumer_only_ns'] / 1_000_000:.3f} | "
            f"{namei['attach_ns'] / 1_000_000:.3f} | "
            f"{entry['publication_ratio']:.6f} |")
    lines.extend([
        "",
        "## namei_ext Setup Attribution",
        "",
        "| Width | Object preparation (ms) | Target registration (ms) | "
        "Map population (ms) | Consumer cgroup move (ms) | Registered targets |",
        "| ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    for entry in summary["scales"]:
        namei = entry["timing_median_ns"]["namei_ext"]
        lines.append(
            f"| {entry['width']} | "
            f"{namei['object_preparation_ns'] / 1_000_000:.3f} | "
            f"{namei['target_registration_ns'] / 1_000_000:.3f} | "
            f"{namei['map_population_ns'] / 1_000_000:.3f} | "
            f"{namei['consumer_cgroup_move_ns'] / 1_000_000:.3f} | "
            f"{entry['registered_targets']} |")
    if all("materialization_median" in entry for entry in summary["scales"]):
        lines.extend([
            "",
            "## Materialization Work",
            "",
            "| Width | AtomicWriter new payload files | AtomicWriter new "
            "payload bytes | namei_ext lower files | namei_ext lower bytes | "
            "namei_ext logical placeholders | namei_ext prepared regular files | "
            "AtomicWriter live files (I/U/N/R) | namei_ext visible files (I/U/N/R) |",
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
        ])
        for entry in summary["scales"]:
            objects = entry["materialization_median"]
            lines.append(
                f"| {entry['width']} | "
                f"{objects['atomicwriter_newly_materialized_files']:.0f} | "
                f"{objects['atomicwriter_newly_materialized_bytes']:.0f} | "
                f"{objects['namei_ext_lower_files']:.0f} | "
                f"{objects['namei_ext_lower_bytes']:.0f} | "
                f"{objects['namei_ext_logical_files']:.0f} | "
                f"{objects['namei_ext_prepared_regular_files']:.0f} | "
                f"{objects['atomicwriter_live_files_initial']:.0f}/"
                f"{objects['atomicwriter_live_files_update']:.0f}/"
                f"{objects['atomicwriter_live_files_no_op']:.0f}/"
                f"{objects['atomicwriter_live_files_rollback']:.0f} | "
                f"{objects['namei_ext_visible_files_initial']:.0f}/"
                f"{objects['namei_ext_visible_files_update']:.0f}/"
                f"{objects['namei_ext_visible_files_no_op']:.0f}/"
                f"{objects['namei_ext_visible_files_rollback']:.0f} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def analyze(run_dir):
    run = load_json(run_dir / "run.json")
    raw_rows = load_jsonl(run_dir / "observations.jsonl")
    allowed = {
        EVENT,
        "kubernetes-configmap-quantitative-identity",
        "kubernetes-configmap-quantitative-lower",
        "kubernetes-configmap-quantitative-unmanaged-directory",
        AUDIT_EVENT,
    }
    for row in raw_rows:
        if row.get("event") not in allowed or row.get("pass") is not True:
            raise ValueError("unexpected or failed raw observation")
    rows = [row for row in raw_rows if row["event"] == EVENT]
    audits = [row for row in raw_rows if row["event"] == AUDIT_EVENT]
    summary, per_boot, pairs = summarize(rows, run, audits)
    analysis = run_dir / "analysis"
    analysis.mkdir()
    write_json(analysis / "summary.json", summary)
    write_jsonl(analysis / "per-boot.jsonl", per_boot)
    write_jsonl(analysis / "pairs.jsonl", pairs)
    write_csv_artifacts(analysis, summary)
    write_report(analysis / "report.md", summary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    arguments = parser.parse_args()
    analyze(arguments.run_dir)


if __name__ == "__main__":
    main()

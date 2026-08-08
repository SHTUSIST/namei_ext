#!/usr/bin/env python3
"""Analysis for the host-baseline per-view supply cost collector.

Reads raw observations, reports percentiles and paired bootstrap confidence
intervals, and writes summary.json plus a Markdown report. It computes; it does
not decide what the paper may claim.
"""

import argparse
import json
import os
import random
import statistics

BOOTSTRAP_RESAMPLES = 10000


def percentile(sorted_values, fraction):
    if not sorted_values:
        raise SystemExit("percentile of empty sample")
    index = min(len(sorted_values) - 1,
                max(0, int(round(fraction * (len(sorted_values) - 1)))))
    return sorted_values[index]


def bootstrap_ci(values, rng, statistic=statistics.median,
                 resamples=BOOTSTRAP_RESAMPLES):
    if not values:
        raise SystemExit("bootstrap of empty sample")
    size = len(values)
    estimates = []
    for _ in range(resamples):
        sample = [values[rng.randrange(size)] for _ in range(size)]
        estimates.append(statistic(sample))
    estimates.sort()
    return (percentile(estimates, 0.025), percentile(estimates, 0.975))


def ratio_ci(numerator, denominator, rng, resamples=BOOTSTRAP_RESAMPLES):
    """Confidence interval for median(numerator)/median(denominator)."""
    if not numerator or not denominator:
        raise SystemExit("ratio of empty sample")
    estimates = []
    for _ in range(resamples):
        num = [numerator[rng.randrange(len(numerator))]
               for _ in range(len(numerator))]
        den = [denominator[rng.randrange(len(denominator))]
               for _ in range(len(denominator))]
        denominator_median = statistics.median(den)
        if denominator_median == 0:
            raise SystemExit("zero denominator in ratio bootstrap")
        estimates.append(statistics.median(num) / denominator_median)
    estimates.sort()
    return (percentile(estimates, 0.025), percentile(estimates, 0.975))


def load(path):
    records = []
    with open(path, "r") as handle:
        for line in handle:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    if not records:
        raise SystemExit("no observations in %s" % path)
    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--result-dir", required=True)
    parser.add_argument("--seed", type=int, default=20260808)
    args = parser.parse_args()

    records = load(os.path.join(args.result_dir, "observations.jsonl"))
    if not any(record["kind"] == "completion" for record in records):
        raise SystemExit("run did not complete; refusing to analyse")
    rng = random.Random(args.seed)

    supply = {}
    for record in records:
        if record["kind"] != "supply":
            continue
        supply.setdefault((record["condition"], record["views"]), []).append(record)

    summary = {"schema": "namei_ext.view_supply_cost_host.summary.v1",
               "supply": [], "page_cache": [], "switch": [],
               "namei_ext_measured": False}

    for (condition, views), group in sorted(supply.items()):
        durations = sorted(value for record in group
                           for value in record["supply_ns"])
        slab_deltas = [record["memory_after"]["slab_kb"]
                       - record["memory_before"]["slab_kb"]
                       for record in group]
        clone = sorted(value for record in group
                       for value in record["mountns_clone_ns"])
        summary["supply"].append({
            "condition": condition,
            "views": views,
            "trials": len(group),
            "samples": len(durations),
            "supply_p50_ns": percentile(durations, 0.50),
            "supply_p95_ns": percentile(durations, 0.95),
            "supply_p99_ns": percentile(durations, 0.99),
            "supply_p50_ci_ns": bootstrap_ci(durations, rng),
            "slab_delta_kb_median": statistics.median(slab_deltas),
            "slab_kb_per_view": statistics.median(slab_deltas) / views,
            "mountns_clone_p50_ns": percentile(clone, 0.50),
            "mountns_clone_p50_ci_ns": bootstrap_ci(clone, rng),
            "host_mountinfo_lines_after":
                statistics.median([record["host_mountinfo_lines_after"]
                                   for record in group]),
        })

    # The per-view cost of a mount namespace plus one bind mount is only
    # meaningful after the cost of the process holding it is removed.
    by_key = {(row["condition"], row["views"]): row for row in summary["supply"]}
    summary["mountns_bind_minus_control"] = []
    for views in sorted({row["views"] for row in summary["supply"]}):
        held = by_key.get(("mountns_bind", views))
        control = by_key.get(("control_process", views))
        if held and control:
            summary["mountns_bind_minus_control"].append({
                "views": views,
                "slab_kb_per_view":
                    (held["slab_delta_kb_median"]
                     - control["slab_delta_kb_median"]) / views,
                "supply_p50_delta_ns":
                    held["supply_p50_ns"] - control["supply_p50_ns"],
            })

    for record in records:
        if record["kind"] == "page_cache":
            for condition, payload in record["results"].items():
                summary["page_cache"].append({
                    "trial": record["trial"], "condition": condition,
                    "views": payload["views"],
                    "bytes_read": payload["bytes_read"],
                    "cached_delta_kb": (payload["cached_after_kb"]
                                        - payload["cached_before_kb"]),
                })
        elif record["kind"] == "switch":
            for observation in record["observations"]:
                if observation["situation"] == "idle":
                    values = sorted(observation["switch_ns"])
                    summary["switch"].append({
                        "trial": record["trial"], "situation": "idle",
                        "p50_ns": percentile(values, 0.50),
                        "p95_ns": percentile(values, 0.95),
                        "p99_ns": percentile(values, 0.99),
                        "p50_ci_ns": bootstrap_ci(values, rng),
                    })
                else:
                    summary["switch"].append({
                        "trial": record["trial"], "situation": "in_use",
                        "umount_returncode": observation["umount_returncode"],
                        "umount_stderr": observation["umount_stderr"],
                    })

    # Page-cache amplification relative to the shared-source bind condition.
    bind = [row["cached_delta_kb"] for row in summary["page_cache"]
            if row["condition"] == "bind_same_source"]
    for condition in ("overlay_same_lower", "btrfs_snapshot"):
        other = [row["cached_delta_kb"] for row in summary["page_cache"]
                 if row["condition"] == condition]
        if bind and other:
            summary.setdefault("page_cache_amplification", []).append({
                "condition": condition,
                "median_ratio_vs_bind":
                    statistics.median(other) / statistics.median(bind),
                "ratio_ci": ratio_ci(other, bind, rng, resamples=2000),
            })

    summary_path = os.path.join(args.result_dir, "summary.json")
    with open(summary_path, "w") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
        handle.write("\n")

    lines = ["# Per-view supply cost on the host kernel", "",
             "namei_ext is not in this table. It needs the patched kernel in "
             "KVM, so its column is absent rather than estimated.", "",
             "## Supply cost and resident cost", "",
             "| condition | views | supply p50 (us) | supply p99 (us) | "
             "slab per view (KB) | next mount-namespace clone p50 (us) |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for row in summary["supply"]:
        lines.append("| %s | %d | %.1f | %.1f | %.2f | %.1f |"
                     % (row["condition"], row["views"],
                        row["supply_p50_ns"] / 1000.0,
                        row["supply_p99_ns"] / 1000.0,
                        row["slab_kb_per_view"],
                        row["mountns_clone_p50_ns"] / 1000.0))

    lines += ["", "## Page cache held by N views of the same content", "",
              "| condition | views | MiB read | MiB of page cache |",
              "| --- | ---: | ---: | ---: |"]
    for row in summary["page_cache"]:
        lines.append("| %s | %d | %.0f | %.1f |"
                     % (row["condition"], row["views"],
                        row["bytes_read"] / 1048576.0,
                        row["cached_delta_kb"] / 1024.0))

    lines += ["", "## Re-pointing a view", ""]
    for row in summary["switch"]:
        if row["situation"] == "idle":
            lines.append("- idle, trial %d: p50 %.1f us, p99 %.1f us"
                         % (row["trial"], row["p50_ns"] / 1000.0,
                            row["p99_ns"] / 1000.0))
        else:
            lines.append("- in use, trial %d: umount returned %d, `%s`"
                         % (row["trial"], row["umount_returncode"],
                            row["umount_stderr"]))

    report_path = os.path.join(args.result_dir, "report.md")
    with open(report_path, "w") as handle:
        handle.write("\n".join(lines) + "\n")

    print(summary_path)
    print(report_path)


if __name__ == "__main__":
    main()

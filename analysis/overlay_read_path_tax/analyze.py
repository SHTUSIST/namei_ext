#!/usr/bin/env python3
"""Analysis for the overlayfs read-path collector.

Reports, per layer depth, what a read-only operation pays for the fact that the
mount also carries a writable layer. Ratios are against the same content read
directly from the lower filesystem.
"""

import argparse
import json
import os
import random
import statistics

BOOTSTRAP_RESAMPLES = 10000
METRICS = ("stat_ns", "open_ns", "readdir_ns", "cold_read_ns")


def percentile(sorted_values, fraction):
    if not sorted_values:
        raise SystemExit("percentile of empty sample")
    index = min(len(sorted_values) - 1,
                max(0, int(round(fraction * (len(sorted_values) - 1)))))
    return sorted_values[index]


# The warm metrics have tens of thousands of samples per condition. Resampling
# all of them ten thousand times is not affordable in plain Python and buys
# nothing: the median's sampling distribution is already tight at this size.
# Each bootstrap replicate therefore draws a fixed number of points, and that
# number is recorded in the summary so the interval can be reproduced.
BOOTSTRAP_DRAW = 4000


def ratio_ci(numerator, denominator, rng, resamples=2000):
    draw_numerator = min(len(numerator), BOOTSTRAP_DRAW)
    draw_denominator = min(len(denominator), BOOTSTRAP_DRAW)
    estimates = []
    for _ in range(resamples):
        num = [numerator[rng.randrange(len(numerator))]
               for _ in range(draw_numerator)]
        den = [denominator[rng.randrange(len(denominator))]
               for _ in range(draw_denominator)]
        base = statistics.median(den)
        if base == 0:
            raise SystemExit("zero denominator in ratio bootstrap")
        estimates.append(statistics.median(num) / base)
    estimates.sort()
    return (percentile(estimates, 0.025), percentile(estimates, 0.975))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--result-dir", required=True)
    parser.add_argument("--seed", type=int, default=20260808)
    args = parser.parse_args()

    path = os.path.join(args.result_dir, "observations.jsonl")
    records = []
    with open(path, "r") as handle:
        for line in handle:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    if not any(record["kind"] == "completion" for record in records):
        raise SystemExit("run did not complete; refusing to analyse")

    rng = random.Random(args.seed)
    pooled = {}
    identity = {}
    depth_of = {}
    for record in records:
        if record["kind"] != "read_path":
            continue
        condition = record["condition"]
        depth_of[condition] = record["depth"]
        identity.setdefault(condition, record["identity"])
        for metric in METRICS:
            pooled.setdefault(condition, {}).setdefault(metric, []).extend(
                record[metric])

    if "native" not in pooled:
        raise SystemExit("no native condition; nothing to compare against")

    summary = {"schema": "namei_ext.overlay_read_path_tax.summary.v1",
               "bootstrap_resamples": 2000,
               "bootstrap_draw_per_replicate": BOOTSTRAP_DRAW,
               "seed": args.seed,
               "conditions": [], "identity": identity}

    for condition in sorted(pooled, key=lambda name: depth_of[name]):
        entry = {"condition": condition, "depth": depth_of[condition]}
        for metric in METRICS:
            values = sorted(pooled[condition][metric])
            entry[metric] = {
                "samples": len(values),
                "p50": percentile(values, 0.50),
                "p95": percentile(values, 0.95),
                "p99": percentile(values, 0.99),
            }
            if condition != "native":
                base = sorted(pooled["native"][metric])
                entry[metric]["ratio_vs_native"] = (
                    percentile(values, 0.50) / percentile(base, 0.50))
                entry[metric]["ratio_ci"] = ratio_ci(values, base, rng)
        summary["conditions"].append(entry)

    with open(os.path.join(args.result_dir, "summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
        handle.write("\n")

    lines = ["# What a read-only operation pays for a writable layer", "",
             "Same content, same host kernel. `native` reads the lower "
             "filesystem directly. `overlay_dN` reads it through an overlayfs "
             "mount with a writable upper directory and N read-only lower "
             "directories, `metacopy` and `redirect_dir` enabled.", "",
             "| condition | depth | stat p50 (ns) | vs native | open p50 (ns) "
             "| vs native | readdir p50 (us) | vs native | cold open+read p50 "
             "(us) | vs native |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for entry in summary["conditions"]:
        def cell(metric, scale, digits):
            value = entry[metric]["p50"] / scale
            if "ratio_vs_native" in entry[metric]:
                low, high = entry[metric]["ratio_ci"]
                return ("%.*f | %.3f [%.3f, %.3f]"
                        % (digits, value, entry[metric]["ratio_vs_native"],
                           low, high))
            return "%.*f | --" % (digits, value)

        lines.append("| %s | %d | %s | %s | %s | %s |"
                     % (entry["condition"], entry["depth"],
                        cell("stat_ns", 1, 0), cell("open_ns", 1, 0),
                        cell("readdir_ns", 1000, 1),
                        cell("cold_read_ns", 1000, 1)))

    lines += ["", "## Object identity as the view reports it", "",
              "| condition | st_dev | st_ino | realpath | /proc/self/fd |",
              "| --- | ---: | ---: | --- | --- |"]
    for condition, info in sorted(identity.items(),
                                  key=lambda item: depth_of[item[0]]):
        lines.append("| %s | %d | %d | `%s` | `%s` |"
                     % (condition, info["st_dev"], info["st_ino"],
                        info["realpath"], info["proc_self_fd"]))

    with open(os.path.join(args.result_dir, "report.md"), "w") as handle:
        handle.write("\n".join(lines) + "\n")

    print(os.path.join(args.result_dir, "summary.json"))
    print(os.path.join(args.result_dir, "report.md"))


if __name__ == "__main__":
    main()

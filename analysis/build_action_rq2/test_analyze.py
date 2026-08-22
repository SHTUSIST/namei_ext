#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

import analyze


class BuildActionRQ2AnalysisTests(unittest.TestCase):
    def plan(self, repetitions=2, samples=3, capacity=0):
        return {
            "repetitions": repetitions,
            "samples": samples,
            "scales": (64, 512, 2048),
            "primary_scale": 2048,
            "capacity": capacity,
        }

    def complete_rows(self, plan):
        rows = []
        for repetition in range(1, plan["repetitions"] + 1):
            offset = (repetition - 1) % len(plan["scales"])
            scales = plan["scales"][offset:] + plan["scales"][:offset]
            for condition in analyze.CONDITIONS:
                order_index = 0
                for scale in scales:
                    for sample in range(1, plan["samples"] + 1):
                        order_index += 1
                        multiplier = 1 if condition == "namei_ext" else 2
                        rows.append({
                            "event": analyze.SAMPLE_EVENT,
                            "condition": condition,
                            "repetition": repetition,
                            "scale": scale,
                            "sample": sample,
                            "order_index": order_index,
                            "setup_ns": multiplier * scale * 1000 + sample,
                            "action_ns": multiplier * scale * 2000 + sample,
                            "lifecycle_ns":
                                multiplier * scale * 4000 + sample,
                            "sandboxfs_user_ticks":
                                0 if condition == "namei_ext" else 1,
                            "sandboxfs_system_ticks": 0,
                            "sandboxfs_voluntary_context_switches": 0,
                            "sandboxfs_involuntary_context_switches": 0,
                            "sandboxfs_vm_hwm_kb":
                                0 if condition == "namei_ext" else 1024,
                            "output_bytes_a": scale * 31,
                            "output_bytes_b": scale * 31,
                            "output_exact": True,
                            "actions": 2,
                            "concurrent": True,
                            "unknown_hidden": True,
                            "undeclared_hidden": True,
                            "lower_objects_unchanged": True,
                            "pass": True,
                        })
                rows.append({
                    "event": analyze.SUMMARY_EVENT,
                    "condition": condition,
                    "repetition": repetition,
                    "scales": len(plan["scales"]),
                    "samples_per_scale": plan["samples"],
                    "completed_samples":
                        len(plan["scales"]) * plan["samples"],
                    "pass": True,
                })
            for counter in analyze.POLICY_COUNTERS:
                rows.append({
                    "event": analyze.COUNTER_EVENT,
                    "condition": "namei_ext",
                    "repetition": repetition,
                    "counter": counter,
                    "value": 0 if counter == 8 else 1,
                    "pass": True,
                })
            if plan["capacity"]:
                rows.append({
                    "event": analyze.CAPACITY_EVENT,
                    "condition": "namei_ext",
                    "repetition": repetition,
                    "requested": plan["capacity"],
                    "inserted": plan["capacity"],
                    "removed": plan["capacity"],
                    "remaining": 0,
                    "pass": True,
                })
        return rows

    def test_complete_matrix_validates(self):
        plan = self.plan()
        indexed, counters, correctness = analyze.validate(
            self.complete_rows(plan), plan)
        self.assertEqual(correctness["boots_observed"], 4)
        self.assertEqual(correctness["samples_observed"], 36)
        self.assertEqual(len(indexed), 2 * 2 * 3 * 3)
        self.assertEqual(counters[1][0], 1)

    def test_missing_sample_is_rejected(self):
        plan = self.plan()
        rows = self.complete_rows(plan)
        rows.pop(0)
        with self.assertRaisesRegex(ValueError, "sample count"):
            analyze.validate(rows, plan)

    def test_inexact_output_is_rejected(self):
        plan = self.plan()
        rows = self.complete_rows(plan)
        sample = next(row for row in rows
                      if row["event"] == analyze.SAMPLE_EVENT)
        sample["output_exact"] = False
        with self.assertRaisesRegex(ValueError, "correctness oracle"):
            analyze.validate(rows, plan)

    def test_failed_observation_is_rejected_while_loading(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rows.jsonl"
            path.write_text(json.dumps({
                "event": analyze.FAILURE_EVENT,
                "condition": "namei_ext",
                "repetition": 1,
                "pass": False,
            }) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "failed or ungraded"):
                analyze.load_rows(path)

    def test_capacity_probe_is_strict(self):
        plan = self.plan(capacity=4096)
        rows = self.complete_rows(plan)
        capacity = next(row for row in rows
                        if row["event"] == analyze.CAPACITY_EVENT)
        capacity["remaining"] = 1
        with self.assertRaisesRegex(ValueError, "capacity probe failed"):
            analyze.validate(rows, plan)

    def test_paired_ratios_and_supported_verdict(self):
        plan = self.plan(repetitions=3)
        indexed, _, _ = analyze.validate(
            self.complete_rows(plan), plan)
        first = {1: "namei_ext", 2: "sandboxfs", 3: "namei_ext"}
        summaries = analyze.summarize(indexed, plan, 7, first)
        primary = next(row for row in summaries
                       if row["scale"] == 2048 and
                       row["metric"] == "action_ns")
        for ratio in primary["paired_ratios"]:
            self.assertAlmostEqual(ratio, 2.0, places=5)
        verdict = analyze.classify(summaries, 2048)
        self.assertEqual(verdict["tested_hypothesis"], "supported")

    def test_interval_crossing_one_is_inconclusive(self):
        summaries = [{
            "scale": 2048,
            "metric": "action_ns",
            "ratio_median": 1.0,
            "ratio_ci_low": 0.9,
            "ratio_ci_high": 1.1,
        }]
        verdict = analyze.classify(summaries, 2048)
        self.assertEqual(verdict["tested_hypothesis"], "inconclusive")

    def test_launch_order_requires_alternation(self):
        rows = []
        for index, condition in enumerate(
                ("namei_ext", "sandboxfs", "sandboxfs", "namei_ext"), 1):
            rows.append({
                "schema":
                    "namei_ext.build_action_rq2.launch_order.v1",
                "order_index": index,
                "repetition": (index - 1) // 2 + 1,
                "condition": condition,
                "host_started_at": f"start-{index}",
                "host_completed_at": f"end-{index}",
            })
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "launch.jsonl"
            path.write_text(
                "".join(json.dumps(row) + "\n" for row in rows),
                encoding="utf-8",
            )
            first = analyze.load_launch_order(path, 2)
            self.assertEqual(
                first, {1: "namei_ext", 2: "sandboxfs"})
            rows[2]["condition"] = "namei_ext"
            path.write_text(
                "".join(json.dumps(row) + "\n" for row in rows),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "launch-order"):
                analyze.load_launch_order(path, 2)

    def test_complete_matrix_generates_all_report_artifacts(self):
        plan = self.plan()
        indexed, _, correctness = analyze.validate(
            self.complete_rows(plan), plan)
        first = {1: "namei_ext", 2: "sandboxfs"}
        summaries = analyze.summarize(indexed, plan, 11, first)
        verdict = analyze.classify(summaries, plan["primary_scale"])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            analyze.write_csv(output / "summary.csv", summaries)
            analyze.write_report(
                output / "report.md", summaries, correctness, verdict,
                plan, 11,
            )
            analyze.write_plot(output, summaries, plan)
            for name in (
                    "summary.csv", "report.md", "action-time.pdf",
                    "action-time.png"):
                self.assertGreater((output / name).stat().st_size, 0)
            report = (output / "report.md").read_text(encoding="utf-8")
            self.assertIn("## Correctness Gate", report)
            self.assertIn("## Tested Hypothesis", report)
            self.assertIn("| 2048 | Action time |", report)


if __name__ == "__main__":
    unittest.main()

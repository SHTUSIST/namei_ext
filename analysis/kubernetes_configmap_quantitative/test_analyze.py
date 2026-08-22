import json
import tempfile
import unittest
from pathlib import Path

import analyze


def lifecycle(boot, width, pair, mechanism, order, elapsed):
    row = {
        "event": analyze.EVENT,
        "mechanism": mechanism,
        "boot": boot,
        "width": width,
        "pair": pair,
        "order": order,
        "active_total_ns": elapsed,
        "wall_span_ns": elapsed,
        "publication_only_ns": 40,
        "consumer_only_ns": 40,
        "phases": {
            "setup_ns": 20,
            "initial_publish_ns": 10,
            "initial_consumer_ns": 10,
            "update_publish_ns": 10,
            "update_consumer_ns": 10,
            "no_op_publish_ns": 10,
            "no_op_consumer_ns": 10,
            "rollback_publish_ns": 10,
            "rollback_consumer_ns": 10,
        },
        "cleanup_pass": True,
        "pass": True,
    }
    if mechanism == "namei_ext":
        row.update({
            "attach_ns": 5,
            "setup_phases": {
                "object_preparation_ns": 5,
                "target_registration_ns": 5,
                "map_population_ns": 5,
                "consumer_cgroup_move_ns": 5,
            },
            "registered_targets": 2 * (width - 1),
            "lower_files": 2 * (width - 1),
            "lower_bytes": 100,
            "logical_files": width,
            "present_per_state": width - 1,
        })
    return row


def audit(boot, width, pair):
    return {
        "event": analyze.AUDIT_EVENT,
        "mechanism": "atomicwriter",
        "boot": boot,
        "width": width,
        "pair": pair,
        "pass": True,
        "materialization": [
            {
                "newly_materialized_files": width - 1 if index != 2 else 0,
                "newly_materialized_bytes": 50 if index != 2 else 0,
                "live_regular_files": width - 1,
                "live_payload_bytes": 50,
            }
            for index in range(4)
        ],
    }


class AnalyzeTest(unittest.TestCase):
    def test_formal_boot_clustering_supports_primary_ratio(self):
        rows = []
        for boot in range(1, 21):
            for pair in range(1, 6):
                rows.append(lifecycle(boot, 256, pair, "atomicwriter", 1,
                                      2_000_000 + boot))
                rows.append(lifecycle(boot, 256, pair, "namei_ext", 2,
                                      1_000_000 + boot))
        run = {"matrix": {"boots": 20, "pairs_per_scale_per_boot": 5,
                           "scales": [256]}}
        summary, per_boot, pairs = analyze.summarize(rows, run)
        self.assertEqual(summary["lifecycle_rows"], 200)
        self.assertEqual(len(per_boot), 20)
        self.assertEqual(len(pairs), 100)
        self.assertLess(summary["scales"][0]["ratio_ci95_upper"], 1.0)
        self.assertEqual(summary["verdict"],
                         "supports-lower-cost-at-primary-scale")

    def test_preflight_has_no_confidence_claim(self):
        rows = [
            lifecycle(1, 16, 1, "atomicwriter", 1, 100),
            lifecycle(1, 16, 1, "namei_ext", 2, 90),
            lifecycle(1, 16, 2, "namei_ext", 1, 95),
            lifecycle(1, 16, 2, "atomicwriter", 2, 105),
        ]
        run = {"matrix": {"boots": 1, "pairs_per_scale_per_boot": 2,
                           "scales": [16]}}
        summary, _, _ = analyze.summarize(rows, run)
        self.assertIsNone(summary["scales"][0]["ratio_ci95_upper"])
        self.assertEqual(summary["verdict"], "preflight-complete")

    def test_primary_uses_begin_to_end_wall_span(self):
        atomic = lifecycle(1, 16, 1, "atomicwriter", 1, 100)
        namei = lifecycle(1, 16, 1, "namei_ext", 2, 50)
        atomic["wall_span_ns"] = 200
        namei["wall_span_ns"] = 300
        run = {"matrix": {"boots": 1, "pairs_per_scale_per_boot": 1,
                           "scales": [16]}}
        summary, _, _ = analyze.summarize([atomic, namei], run)
        self.assertEqual(summary["primary_metric"], "wall_span_ns")
        self.assertAlmostEqual(summary["scales"][0]["ratio"], 1.5)

    def test_reports_secondary_timing_and_materialization(self):
        rows = [
            lifecycle(1, 16, 1, "atomicwriter", 1, 100),
            lifecycle(1, 16, 1, "namei_ext", 2, 50),
        ]
        rows[0]["publication_only_ns"] = 80
        rows[1]["publication_only_ns"] = 20
        run = {"matrix": {"boots": 1, "pairs_per_scale_per_boot": 1,
                           "scales": [16]}}
        summary, per_boot, pairs = analyze.summarize(
            rows, run, [audit(1, 16, 1)])
        scale = summary["scales"][0]
        self.assertAlmostEqual(scale["publication_ratio"], 0.25)
        self.assertEqual(scale["timing_median_ns"]["namei_ext"]["attach_ns"], 5)
        self.assertEqual(
            scale["timing_median_ns"]["namei_ext"]["target_registration_ns"],
            5)
        self.assertEqual(scale["registered_targets"], 30)
        self.assertEqual(
            scale["materialization_median"][
                "atomicwriter_newly_materialized_files"], 45)
        self.assertEqual(
            scale["materialization_median"][
                "namei_ext_prepared_regular_files"], 46)
        self.assertEqual(
            scale["materialization_median"][
                "atomicwriter_live_files_no_op"], 15)
        self.assertEqual(
            scale["materialization_median"][
                "namei_ext_visible_files_rollback"], 15)
        self.assertIn("timing_median_ns", per_boot[0])
        self.assertIn("materialization", pairs[0])

    def test_rejects_unpaired_rows(self):
        rows = [lifecycle(1, 16, 1, "atomicwriter", 1, 100)] * 4
        run = {"matrix": {"boots": 1, "pairs_per_scale_per_boot": 2,
                           "scales": [16]}}
        with self.assertRaises(ValueError):
            analyze.summarize(rows, run)

    def test_rejects_nonadditive_namei_setup(self):
        rows = [
            lifecycle(1, 16, 1, "atomicwriter", 1, 100),
            lifecycle(1, 16, 1, "namei_ext", 2, 90),
        ]
        rows[1]["setup_phases"]["map_population_ns"] += 1
        run = {"matrix": {"boots": 1, "pairs_per_scale_per_boot": 1,
                           "scales": [16]}}
        with self.assertRaisesRegex(ValueError, "setup decomposition"):
            analyze.summarize(rows, run)

    def test_writes_analysis_artifacts(self):
        rows = [
            lifecycle(1, 16, 1, "atomicwriter", 1, 100),
            lifecycle(1, 16, 1, "namei_ext", 2, 90),
            lifecycle(1, 16, 2, "namei_ext", 1, 95),
            lifecycle(1, 16, 2, "atomicwriter", 2, 105),
        ]
        run = {"matrix": {"boots": 1, "pairs_per_scale_per_boot": 2,
                           "scales": [16]}}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "run.json").write_text(json.dumps(run), encoding="utf-8")
            with (root / "observations.jsonl").open("w", encoding="utf-8") as output:
                for row in [*rows, audit(1, 16, 1), audit(1, 16, 2)]:
                    output.write(json.dumps(row) + "\n")
            analyze.analyze(root)
            self.assertTrue((root / "analysis" / "summary.json").is_file())
            self.assertTrue((root / "analysis" / "report.md").is_file())
            self.assertTrue((root / "analysis" / "scaling.csv").is_file())
            self.assertTrue((root / "analysis" / "decomposition.csv").is_file())
            self.assertTrue((root / "analysis" / "materialization.csv").is_file())
            materialization = (root / "analysis" / "materialization.csv").read_text(
                encoding="utf-8")
            decomposition = (root / "analysis" / "decomposition.csv").read_text(
                encoding="utf-8")
            report = (root / "analysis" / "report.md").read_text(
                encoding="utf-8")
            self.assertIn("atomicwriter_live_files_no_op", materialization)
            self.assertIn("namei_ext_visible_files_rollback", materialization)
            self.assertIn("target_registration_ns", decomposition)
            self.assertIn("namei_ext Setup Attribution", report)


if __name__ == "__main__":
    unittest.main()

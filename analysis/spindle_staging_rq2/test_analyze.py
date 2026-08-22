import errno
import tempfile
import unittest
from pathlib import Path

from analyze import analyze, write_outputs


def fixture(repetitions=3, samples=5, fuse_ratio=2.0):
    run = {
        "matrix": {
            "conditions": ["namei_ext", "fuse"],
            "repetitions": repetitions,
            "measured_samples_per_boot": samples,
        }
    }
    launches = []
    observations = []
    order_index = 0
    for repetition in range(1, repetitions + 1):
        order = ("namei_ext", "fuse") if repetition % 2 else ("fuse", "namei_ext")
        for condition in order:
            order_index += 1
            launches.append(
                {
                    "order_index": order_index,
                    "repetition": repetition,
                    "condition": condition,
                }
            )
        for condition in ("namei_ext", "fuse"):
            multiplier = fuse_ratio if condition == "fuse" else 1.0
            observations.extend(
                [
                    {
                        "event": "spindle-staging-rq2-lower-filesystem",
                        "runtime_fstype": "tmpfs",
                        "condition": condition,
                        "repetition": repetition,
                        "pass": True,
                    },
                    {
                        "event": "spindle-staging-rq2-lifecycle",
                        "phase": "setup",
                        "duration_ns": 100 + repetition,
                        "condition": condition,
                        "repetition": repetition,
                        "pass": True,
                    },
                    {
                        "event": "spindle-staging-rq2-lifecycle",
                        "phase": "teardown",
                        "duration_ns": 50 + repetition,
                        "condition": condition,
                        "repetition": repetition,
                        "pass": True,
                    },
                    {
                        "event": "spindle-staging-rq2-permission",
                        "observed_errno": errno.EACCES,
                        "restore_errno": 0,
                        "condition": condition,
                        "repetition": repetition,
                        "pass": True,
                    },
                    {
                        "event": "spindle-staging-rq2-withdrawal-lookup",
                        "operation": "fstatat",
                        "observed_errno": errno.ENOENT,
                        "expected_errno": errno.ENOENT,
                        "condition": condition,
                        "repetition": repetition,
                        "pass": True,
                    },
                    {
                        "event": "spindle-staging-rq2-withdrawal",
                        "exit_status": 1,
                        "runner_errno": 0,
                        "expected_diagnostic": True,
                        "condition": condition,
                        "repetition": repetition,
                        "pass": True,
                    },
                    {
                        "event": "spindle-staging-rq2-withdrawal-window",
                        "before": 10,
                        "after": 10,
                        **(
                            {"hide_before": 0, "hide_after": 2}
                            if condition == "namei_ext"
                            else {}
                        ),
                        "condition": condition,
                        "repetition": repetition,
                        "pass": True,
                    },
                ]
            )
            for iteration in range(1, samples + 1):
                observations.append(
                    {
                        "event": "spindle-staging-rq2-sample",
                        "phase": "measured",
                        "condition": condition,
                        "repetition": repetition,
                        "iteration": iteration,
                        "duration_ns": int((1000 + repetition + iteration) * multiplier),
                        "user_cpu_ns": 100,
                        "system_cpu_ns": 50,
                        "minor_faults": 2,
                        "major_faults": 0,
                        "voluntary_context_switches": 3,
                        "involuntary_context_switches": 0,
                        "pass": True,
                    }
                )
        observations.append(
            {
                "event": "spindle-staging-rq2-namei-window",
                "condition": "namei_ext",
                "repetition": repetition,
                "select_delta": samples * 68,
                "pass": True,
            }
        )
        observations.append(
            {
                "event": "spindle-staging-rq2-fuse-counter",
                "counter": "total",
                "condition": "fuse",
                "repetition": repetition,
                "delta": samples * 265,
                "pass": True,
            }
        )
        for phase in ("mode_zero", "mode_restore", "withdraw"):
            observations.append(
                {
                    "event": "spindle-staging-rq2-fuse-invalidation",
                    "phase": phase,
                    "status": 0,
                    "inode_status": 0,
                    "entry_status": 0,
                    "epoch_attempted": False,
                    "epoch_status": 0,
                    "condition": "fuse",
                    "repetition": repetition,
                    "pass": True,
                }
            )
        observations.append(
            {
                "event": "spindle-staging-rq2-fuse-resource",
                "condition": "fuse",
                "repetition": repetition,
                "cpu_runtime_ns": 10000 + repetition,
                "voluntary_context_switches": 2,
                "involuntary_context_switches": 1,
                "pass": True,
            }
        )
    return observations, run, launches


class AnalyzeTest(unittest.TestCase):
    def test_supported_ratio_uses_boot_pairs(self):
        observations, run, launches = fixture()
        summary = analyze(observations, run, launches, seed=7)
        self.assertEqual(summary["primary"]["verdict"], "supported")
        self.assertAlmostEqual(
            summary["primary"]["geometric_mean_ratio"], 2.0, places=3
        )
        self.assertEqual(len(summary["pair_rows"]), 3)

    def test_missing_boot_samples_are_rejected(self):
        observations, run, launches = fixture()
        sample = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-sample"
        )
        observations.remove(sample)
        with self.assertRaisesRegex(ValueError, "has 4 samples"):
            analyze(observations, run, launches, seed=7)

    def test_failed_oracle_is_rejected_before_timing(self):
        observations, run, launches = fixture()
        observations[0]["pass"] = False
        with self.assertRaisesRegex(ValueError, "oracle failed"):
            analyze(observations, run, launches, seed=7)

    def test_non_tmpfs_runtime_is_rejected(self):
        observations, run, launches = fixture()
        lower = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-lower-filesystem"
        )
        lower["runtime_fstype"] = "9p"
        with self.assertRaisesRegex(ValueError, "guest-local tmpfs"):
            analyze(observations, run, launches, seed=7)

    def test_absent_entry_invalidation_is_accepted(self):
        observations, run, launches = fixture()
        invalidation = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-fuse-invalidation"
        )
        invalidation["entry_status"] = -errno.ENOENT
        invalidation["epoch_attempted"] = True
        summary = analyze(observations, run, launches, seed=7)
        self.assertEqual(summary["primary"]["verdict"], "supported")

    def test_absent_entry_requires_epoch_fallback(self):
        observations, run, launches = fixture()
        invalidation = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-fuse-invalidation"
        )
        invalidation["entry_status"] = -errno.ENOENT
        with self.assertRaisesRegex(ValueError, "epoch fallback failed"):
            analyze(observations, run, launches, seed=7)

    def test_epoch_fallback_error_is_rejected(self):
        observations, run, launches = fixture()
        invalidation = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-fuse-invalidation"
        )
        invalidation["entry_status"] = -errno.ENOENT
        invalidation["epoch_attempted"] = True
        invalidation["epoch_status"] = -errno.EIO
        with self.assertRaisesRegex(ValueError, "epoch fallback failed"):
            analyze(observations, run, launches, seed=7)

    def test_absent_entry_requires_withdrawal_lookup_oracle(self):
        observations, run, launches = fixture()
        observations[:] = [
            row
            for row in observations
            if not (
                row["event"] == "spindle-staging-rq2-withdrawal-lookup"
                and row["condition"] == "fuse"
                and row["repetition"] == 1
            )
        ]
        with self.assertRaisesRegex(ValueError, "withdrawn pathname"):
            analyze(observations, run, launches, seed=7)

    def test_stale_withdrawn_path_is_rejected(self):
        observations, run, launches = fixture()
        lookup = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-withdrawal-lookup"
            and row["condition"] == "fuse"
        )
        lookup["observed_errno"] = 0
        with self.assertRaisesRegex(ValueError, "withdrawn pathname"):
            analyze(observations, run, launches, seed=7)

    def test_namei_withdrawal_requires_hide_engagement(self):
        observations, run, launches = fixture()
        window = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-withdrawal-window"
            and row["condition"] == "namei_ext"
        )
        window["hide_after"] = window["hide_before"]
        with self.assertRaisesRegex(ValueError, "must hide the name"):
            analyze(observations, run, launches, seed=7)

    def test_other_entry_invalidation_error_is_rejected(self):
        observations, run, launches = fixture()
        invalidation = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-fuse-invalidation"
        )
        invalidation["entry_status"] = -errno.EIO
        with self.assertRaisesRegex(ValueError, "invalidation.*failed"):
            analyze(observations, run, launches, seed=7)

    def test_inode_invalidation_error_is_rejected(self):
        observations, run, launches = fixture()
        invalidation = next(
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-fuse-invalidation"
        )
        invalidation["inode_status"] = -errno.EIO
        with self.assertRaisesRegex(ValueError, "invalidation.*failed"):
            analyze(observations, run, launches, seed=7)

    def test_out_of_range_mechanism_window_is_rejected(self):
        for event in (
            "spindle-staging-rq2-namei-window",
            "spindle-staging-rq2-fuse-counter",
        ):
            with self.subTest(event=event):
                observations, run, launches = fixture()
                window = next(row for row in observations if row["event"] == event)
                window["repetition"] = run["matrix"]["repetitions"] + 1
                with self.assertRaisesRegex(ValueError, "window"):
                    analyze(observations, run, launches, seed=7)

    def test_duplicate_iteration_is_rejected(self):
        observations, run, launches = fixture()
        samples = [
            row
            for row in observations
            if row["event"] == "spindle-staging-rq2-sample"
            and row["repetition"] == 1
            and row["condition"] == "namei_ext"
        ]
        samples[1]["iteration"] = samples[0]["iteration"]
        with self.assertRaisesRegex(ValueError, "duplicate iteration"):
            analyze(observations, run, launches, seed=7)

    def test_writes_required_artifacts(self):
        observations, run, launches = fixture()
        summary = analyze(observations, run, launches, seed=7)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "analysis"
            write_outputs(summary, output)
            self.assertTrue((output / "summary.json").is_file())
            self.assertTrue((output / "summary.csv").is_file())
            self.assertIn("paired-bootstrap", (output / "report.md").read_text())


if __name__ == "__main__":
    unittest.main()

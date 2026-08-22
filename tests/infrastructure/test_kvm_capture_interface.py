#!/usr/bin/env python3

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class KvmCaptureInterfaceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="namei-ext-kvm-capture-"
        )
        self.root = Path(self.temporary.name)
        self.boot = self.root / "boot"
        self.run = self.root / "run"
        self.boot.mkdir()
        self.run.mkdir()
        self.image = self.root / "bzImage"
        self.image.write_text("fixture\n", encoding="utf-8")
        self.argv = self.root / "vng-argv.json"
        self.vng = self.root / "fake-vng"
        self.vng.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, sys\n"
            "from pathlib import Path\n"
            "Path(os.environ['NAMEI_EXT_TEST_VNG_ARGV']).write_text(\n"
            "    json.dumps(sys.argv[1:]), encoding='utf-8')\n"
            "print('fake vng stdout')\n"
            "print('fake vng stderr', file=sys.stderr)\n"
            "raise SystemExit(int(os.environ.get('NAMEI_EXT_TEST_VNG_EXIT', 0)))\n",
            encoding="utf-8",
        )
        self.vng.chmod(0o755)

    def tearDown(self):
        self.temporary.cleanup()

    def invoke(
        self, exit_status: int, recorded_run_id: str = "kvm-capture-contract"
    ):
        (self.run / "run.json").write_text(
            json.dumps({"run_id": recorded_run_id, "status": "running"}) + "\n",
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment["NAMEI_EXT_TEST_VNG_ARGV"] = str(self.argv)
        environment["NAMEI_EXT_TEST_VNG_EXIT"] = str(exit_status)
        return subprocess.run(
            [
                "make",
                "--no-print-directory",
                "-C",
                str(ROOT),
                "__namei_ext_kvm_capture",
                f"VNG={self.vng}",
                "RUN_ID=kvm-capture-contract",
                f"NAMEI_EXT_KVM_CAPTURE_IMAGE={self.image}",
                "NAMEI_EXT_KVM_CAPTURE_GUEST_TARGET=__fixture_guest",
                f"NAMEI_EXT_KVM_CAPTURE_BOOT_DIR={self.boot}",
                f"NAMEI_EXT_KVM_CAPTURE_RUN_DIR={self.run}",
            ],
            check=False,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def test_outer_target_forwards_generated_run_id(self):
        outer_makefile = self.root / "outer.mk"
        outer_makefile.write_text(
            ".PHONY: __contract_outer_kvm_capture\n"
            "__contract_outer_kvm_capture:\n"
            '\tinstall -d "$(TEST_BOOT)" "$(TEST_RUN)"\n'
            "\tprintf '%s\\n' "
            '\'{"run_id":"$(RUN_ID)","status":"running"}\' '
            '> "$(TEST_RUN)/run.json"\n'
            '\t$(MAKE) --no-print-directory -C "$(ROOT_DIR)" '
            "__namei_ext_kvm_capture \\\n"
            '\t\tRUN_ID="$(RUN_ID)" \\\n'
            '\t\tNAMEI_EXT_KVM_CAPTURE_IMAGE="$(TEST_IMAGE)" \\\n'
            "\t\tNAMEI_EXT_KVM_CAPTURE_GUEST_TARGET=__fixture_guest \\\n"
            '\t\tNAMEI_EXT_KVM_CAPTURE_BOOT_DIR="$(TEST_BOOT)" \\\n'
            '\t\tNAMEI_EXT_KVM_CAPTURE_RUN_DIR="$(TEST_RUN)"\n',
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment["NAMEI_EXT_TEST_VNG_ARGV"] = str(self.argv)
        environment["NAMEI_EXT_TEST_VNG_EXIT"] = "0"
        completed = subprocess.run(
            [
                "make",
                "--no-print-directory",
                "-C",
                str(ROOT),
                "-f",
                "Makefile",
                "-f",
                str(outer_makefile),
                "__contract_outer_kvm_capture",
                f"VNG={self.vng}",
                f"TEST_IMAGE={self.image}",
                f"TEST_BOOT={self.boot}",
                f"TEST_RUN={self.run}",
            ],
            check=False,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        run_id = json.loads(
            (self.run / "run.json").read_text(encoding="utf-8")
        )["run_id"]
        self.assertRegex(run_id, re.compile(r"^\d{8}T\d{6}Z-[0-9a-f]{8}$"))
        arguments = json.loads(self.argv.read_text(encoding="utf-8"))
        guest = arguments[arguments.index("--exec") + 1]
        self.assertIn(f"RUN_ID={run_id}", guest)

    def test_named_interface_captures_launcher_output(self):
        completed = self.invoke(0)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        arguments = json.loads(self.argv.read_text(encoding="utf-8"))
        self.assertEqual(arguments[arguments.index("--run") + 1], str(self.image))
        guest = arguments[arguments.index("--exec") + 1]
        self.assertIn("__fixture_guest", guest)
        self.assertIn("RUN_ID=kvm-capture-contract", guest)
        self.assertEqual(
            (self.boot / "launcher.stdout.log").read_text(encoding="utf-8"),
            "fake vng stdout\n",
        )
        self.assertEqual(
            (self.boot / "launcher.stderr.log").read_text(encoding="utf-8"),
            "fake vng stderr\n",
        )
        self.assertEqual(
            json.loads((self.run / "run.json").read_text())["status"],
            "running",
        )

    def test_named_interface_marks_launcher_failure(self):
        completed = self.invoke(7)
        self.assertNotEqual(completed.returncode, 0)
        run = json.loads((self.run / "run.json").read_text(encoding="utf-8"))
        self.assertEqual(run["status"], "failed")
        self.assertEqual(run["failure"], "kvm-launch-or-guest-command")
        self.assertIn("failed_at", run)

    def test_named_interface_rejects_mismatched_run_id(self):
        completed = self.invoke(0, recorded_run_id="different-run")
        self.assertNotEqual(completed.returncode, 0)
        self.assertFalse(self.argv.exists())
        run = json.loads((self.run / "run.json").read_text(encoding="utf-8"))
        self.assertEqual(run["status"], "running")

    def test_suites_do_not_use_positional_capture_interface(self):
        for directory in (ROOT / "mk/experiments", ROOT / "mk/benchmarks"):
            for path in directory.glob("*.mk"):
                with self.subTest(path=path.relative_to(ROOT)):
                    source = path.read_text(encoding="utf-8")
                    self.assertNotIn(
                        "$(call NAMEI_EXT_KVM_RUN_CAPTURE",
                        source,
                    )
                    lines = source.splitlines()
                    for index, line in enumerate(lines):
                        if "__namei_ext_kvm_capture" not in line:
                            continue
                        call = "\n".join(lines[index:index + 4])
                        self.assertIn('RUN_ID="$(RUN_ID)"', call)

    def test_external_inventory_uses_shared_capture_helper(self):
        shared = (ROOT / "mk/multi_boot.mk").read_text(encoding="utf-8")
        self.assertIn(
            "define NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY",
            shared,
        )
        for required in (
            '"$(strip $(2))" -j prog show',
            '"$(strip $(2))" -j cgroup tree',
            'bpf-programs-$(strip $(3)).json',
            'bpf-cgroup-$(strip $(3)).json',
            'fuse-mounts-$(strip $(3)).txt',
            'fuse-open-fds-$(strip $(3)).txt',
            'fuse-open-fds-$(strip $(3)).status',
        ):
            self.assertIn(required, shared)

        for relative in (
            "mk/benchmarks/fxmark.mk",
            "mk/experiments/build_action_rq2.mk",
        ):
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertGreaterEqual(
                source.count(
                    "$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY"
                ),
                3 if relative.endswith("build_action_rq2.mk") else 2,
                relative,
            )
            self.assertNotIn("findmnt -rn -o FSTYPE,TARGET", source)
            self.assertNotIn("lsof -Fpc /dev/fuse", source)
            self.assertIn("fuse-open-fds-before.status", source)
            self.assertIn("fuse-open-fds-after.status", source)

        checkpoint = (
            ROOT / "mk/experiments/checkpoint_restore.mk"
        ).read_text(encoding="utf-8")
        self.assertEqual(checkpoint.count(' -j prog show \\\n'), 3)
        self.assertEqual(checkpoint.count(' -j cgroup tree \\\n'), 3)
        self.assertNotIn("fuse-mounts", checkpoint)
        self.assertNotIn("fuse-open-fds", checkpoint)

    def test_external_inventory_strips_multiline_call_arguments(self):
        makefile = self.root / "inventory.mk"
        result_dir = self.root / "inventory"
        makefile.write_text(
            f"include {ROOT / 'mk/multi_boot.mk'}\n"
            ".PHONY: render\n"
            "render:\n"
            "\t$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,\\\n"
            "\t\t$(TEST_RESULT_DIR),\\\n"
            "\t\t$(TEST_BPFTOOL),before)\n",
            encoding="utf-8",
        )
        completed = subprocess.run(
            [
                "make",
                "--no-print-directory",
                "-n",
                "-f",
                str(makefile),
                "render",
                f"TEST_RESULT_DIR={result_dir}",
                "TEST_BPFTOOL=/usr/bin/true",
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn(f'test -d "{result_dir}"', completed.stdout)
        self.assertIn('"/usr/bin/true" -j prog show', completed.stdout)
        self.assertNotIn(f'test -d " {result_dir}"', completed.stdout)
        self.assertNotIn('" /usr/bin/true" -j', completed.stdout)

    def test_sandboxfs_build_is_source_pinned_and_locked(self):
        config = (
            ROOT / "configs/benchmarks/workload-sources.mk"
        ).read_text(encoding="utf-8")
        workload = (ROOT / "mk/workload.mk").read_text(encoding="utf-8")
        kernel = (ROOT / "mk/kernel.mk").read_text(encoding="utf-8")
        suite = (
            ROOT / "mk/experiments/build_action_rq2.mk"
        ).read_text(encoding="utf-8")
        runner = (
            ROOT
            / "experiments/build_action_sandboxing/namei_ext_build_action_rq2.c"
        ).read_text(encoding="utf-8")
        normalized_suite = " ".join(suite.replace("\\\n", " ").split())
        lock = ROOT / "thirdparty/locks/sandboxfs-0.2.0.Cargo.lock"

        self.assertRegex(
            config,
            r"SANDBOXFS_COMMIT \?= [0-9a-f]{40}",
        )
        self.assertNotIn("SANDBOXFS_ARCHIVE_SHA256", config)
        self.assertNotIn("SANDBOXFS_CARGO_LOCK_SHA256", config)
        self.assertNotIn("SANDBOXFS_BINARY_SHA256", config)
        self.assertNotIn("SANDBOXFS_ARCHIVE_SHA256", workload)
        self.assertNotIn("SANDBOXFS_CARGO_LOCK_SHA256", workload)
        self.assertNotIn("SANDBOXFS_BINARY_SHA256", workload)
        self.assertNotIn("sha256", suite.lower())
        self.assertNotIn(
            "NAMEI_EXT_MULTI_BOOT_CAPTURE_PINNED_HOST,", suite
        )
        self.assertNotIn("NAMEI_EXT_RUN_VALIDATE_BASE", suite)
        self.assertIn("BUILD_ACTION_RQ2_CAPTURE_HOST", suite)
        self.assertIn("BUILD_ACTION_RQ2_VALIDATE_RUN_BASE", suite)
        self.assertIn(
            "thirdparty/locks/sandboxfs-0.2.0.Cargo.lock",
            config,
        )
        self.assertIn("cargo build --release --locked", workload)
        self.assertIn('test ! -e "$(SANDBOXFS_SRC)/Cargo.lock"', workload)
        self.assertIn("kernel-bpftool:", kernel)
        self.assertIn(
            "grep -Fq 'BPF_CGROUP_NAMEI_EXT,'",
            kernel,
        )
        self.assertIn(
            '"$(KERNEL_DIR)/tools/bpf/bpftool/cgroup.c"',
            kernel,
        )
        self.assertIn(
            "grep -aFq 'cgroup/namei_ext' \"$(KERNEL_BPFTOOL)\"",
            kernel,
        )
        self.assertIn(
            "BUILD_ACTION_RQ2_BPFTOOL ?= $(KERNEL_BPFTOOL)",
            suite,
        )
        self.assertIn("bpftool_source_commit:$$kernel_commit", suite)
        self.assertIn(
            '--arg bpftool_version "$$("$(1)/artifacts/runtime/bpftool" version)"',
            normalized_suite,
        )
        self.assertIn(
            "kvm-build-action-rq2-preflight: experiment-source-clean",
            suite,
        )
        self.assertIn(
            "kvm-build-action-rq2-preflight: kernel-provenance "
            "kernel-bpftool",
            normalized_suite,
        )
        self.assertIn(
            "kvm-build-action-rq2: kernel-provenance kernel-bpftool",
            normalized_suite,
        )
        self.assertNotIn(
            "BUILD_ACTION_RQ2_BPFTOOL ?= /usr/local/sbin/bpftool",
            suite,
        )
        self.assertIn("realpath(argv[4], bazel_path)", runner)
        self.assertIn("bazel = bazel_path;", runner)
        self.assertLess(
            runner.index("realpath(argv[4], bazel_path)"),
            runner.index("bazel = bazel_path;"),
        )
        self.assertLess(
            runner.index("bazel = bazel_path;"),
            runner.rindex("ret = run_sample("),
        )
        self.assertEqual(runner.count("\"printf '%%s' '%s' > '%s'; \""), 2)
        self.assertEqual(runner.count("\"printf '%%s' '%s' > '%s'\\\",\\n\""), 1)
        self.assertNotIn("sha256sum", runner)
        self.assertNotIn("--ttl=0s", runner)
        self.assertIn("verify_action_output", runner)
        self.assertIn(r'\"action\":\"%s\"', runner)
        self.assertIn(r'\"exit_code\":%d,', runner)
        self.assertIn(r'\"signal\":%d,\"pass\":false}', runner)
        self.assertIn("terminate_action(&action_a)", runner)
        self.assertIn("terminate_action(&action_b)", runner)
        self.assertIn("*failed_action_out = actions[index]", runner)
        self.assertIn("record_action_status(action, status)", runner)
        self.assertTrue(lock.is_file())
        self.assertGreater(lock.stat().st_size, 0)


if __name__ == "__main__":
    unittest.main()

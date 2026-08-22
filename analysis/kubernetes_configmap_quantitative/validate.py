#!/usr/bin/env python3

import argparse
import json
from collections import defaultdict
from pathlib import Path


LIFECYCLE = "kubernetes-configmap-quantitative-lifecycle"
IDENTITY = "kubernetes-configmap-quantitative-identity"
LOWER = "kubernetes-configmap-quantitative-lower"
UNMANAGED_DIRECTORY = "kubernetes-configmap-quantitative-unmanaged-directory"
MATERIALIZATION_AUDIT = "kubernetes-configmap-quantitative-materialization-audit"
STATES = ("initial", "update", "no-op", "rollback")
REPLAY_STATES = (
    "initial-replay", "update-replay", "no-op-replay", "rollback-replay"
)
SETUP_PHASES = {
    "object_preparation_ns", "target_registration_ns", "map_population_ns",
    "consumer_cgroup_move_ns",
}


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


def payloads(width):
    v0 = {
        "config/app.conf": ("version=0\n", 0o644),
        "tls/cert.pem": ("certificate-v0\n", 0o400),
        "retired.conf": ("retired\n", 0o644),
    }
    v1 = {
        "config/app.conf": ("version=1\n", 0o600),
        "tls/cert.pem": ("certificate-v1\n", 0o400),
        "added.conf": ("added\n", 0o644),
    }
    for index in range(width - 4):
        path = f"entry-{index:03d}.conf"
        value = (f"stable-{index:03d}\n", 0o644)
        v0[path] = value
        v1[path] = value
    return v0, v1


def exact_names(payload):
    root = sorted({path.split("/", 1)[0] for path in payload})
    config = sorted(path.split("/", 1)[1] for path in payload
                    if path.startswith("config/"))
    tls = sorted(path.split("/", 1)[1] for path in payload
                 if path.startswith("tls/"))
    return root, config, tls


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_consumer(row):
    width = row["width"]
    uid = row["runtime_uid"]
    gid = row["runtime_gid"]
    v0, v1 = payloads(width)
    union = set(v0) | set(v1)
    consumer = row.get("consumer", [])
    require([entry.get("state") for entry in consumer] == list(STATES),
            "consumer state sequence differs from the declared lifecycle")
    for index, entry in enumerate(consumer):
        state = STATES[index]
        payload = v1 if state in ("update", "no-op") else v0
        absent = list(union - set(payload))
        require(len(absent) == 1, "each generation must hide one union path")
        require(entry.get("pass") is True and entry.get("error") == 0,
                f"consumer rejected {state}")
        require(entry.get("readdir_ops") == 3 and
                entry.get("read_ops") == width - 1 and
                entry.get("stat_ops") == width + 1 and
                entry.get("missing_ops") == 1 and
                entry.get("visible_root_entries") == width - 1,
                f"consumer operation counts differ for {state}")
        expected_open = width if state == "initial" else width - 1
        expected_old = 0 if state == "initial" else 1
        require(entry.get("open_ops") == expected_open and
                entry.get("old_fd_ops") == expected_old and
                entry.get("old_bytes") ==
                ("" if state == "initial" else "version=0\n"),
                f"consumer descriptor counts differ for {state}")
        if state == "initial":
            require(entry.get("old_error") == 0 and
                    entry.get("old_mode") == 0 and entry.get("old_uid") == 0 and
                    entry.get("old_gid") == 0 and entry.get("old_size") == 0,
                    "initial old-descriptor evidence is not empty")
        else:
            require(entry.get("old_error") == 0 and
                    entry.get("old_mode") == 0o644 and
                    entry.get("old_uid") == uid and entry.get("old_gid") == gid and
                    entry.get("old_size") == len("version=0\n"),
                    f"old-descriptor metadata differs for {state}")
        files = entry.get("files", [])
        require(len(files) == width, f"consumer raw file count differs for {state}")
        by_path = {item.get("path"): item for item in files}
        require(len(by_path) == width and set(by_path) == union,
                f"consumer raw file set differs for {state}")
        for path, (expected_bytes, expected_mode) in payload.items():
            item = by_path[path]
            require(item.get("error") == 0 and
                    item.get("bytes") == expected_bytes and
                    item.get("mode") == expected_mode and
                    item.get("uid") == uid and item.get("gid") == gid and
                    item.get("size") == len(expected_bytes) and
                    item.get("dev", 0) > 0 and item.get("ino", 0) > 0,
                    f"consumer bytes or metadata differ for {state}:{path}")
        hidden = by_path[absent[0]]
        require(hidden == {
            "path": absent[0], "bytes": "", "error": 2, "mode": 0,
            "uid": 0, "gid": 0, "dev": 0, "ino": 0, "size": 0,
        }, f"consumer hidden-path evidence differs for {state}:{absent[0]}")
        root, config, tls = exact_names(payload)
        require(sorted(entry.get("root_entries", [])) == root and
                sorted(entry.get("config_entries", [])) == config and
                sorted(entry.get("tls_entries", [])) == tls,
                f"consumer directory view differs for {state}")
    first = consumer[0]
    require(all(entry.get("root_dev") == first.get("root_dev") and
                entry.get("root_ino") == first.get("root_ino") and
                entry.get("old_dev") == first.get("app_dev") and
                entry.get("old_ino") == first.get("app_ino")
                for entry in consumer),
            "persistent root or old descriptor identity changed")
    require(consumer[1].get("app_dev") == consumer[2].get("app_dev") and
            consumer[1].get("app_ino") == consumer[2].get("app_ino"),
            "no-op changed the selected app object")
    state_files = [
        {item["path"]: item for item in entry["files"]}
        for entry in consumer
    ]
    for path in v1:
        update = state_files[1][path]
        no_op = state_files[2][path]
        require((update["dev"], update["ino"]) ==
                (no_op["dev"], no_op["ino"]),
                f"no-op changed selected object identity for {path}")
    if row["mechanism"] == "atomicwriter":
        require((consumer[3].get("app_dev"), consumer[3].get("app_ino")) !=
                (consumer[0].get("app_dev"), consumer[0].get("app_ino")),
                "AtomicWriter rollback did not materialize a new generation")
    else:
        for path in v0:
            initial = state_files[0][path]
            rollback = state_files[3][path]
            require((rollback["dev"], rollback["ino"]) ==
                    (initial["dev"], initial["ino"]),
                    f"namei_ext rollback did not select original V0 for {path}")


def validate_atomicwriter(row, audit_rows):
    width = row["width"]
    v0, v1 = payloads(width)
    payload_bytes = [
        sum(len(data) for data, _ in payload.values())
        for payload in (v0, v1, v1, v0)
    ]
    require(row.get("consumer_exit_status") == 0 and
            row.get("cleanup_root_absent") is True and
            row.get("cleanup_root_remove_error") == 0 and
            row.get("cleanup_root_stat_error") == 2 and
            row.get("cleanup_parent_read_error") == 0 and
            row.get("cleanup_parent_entries") == [],
            "AtomicWriter raw cleanup evidence differs")
    require(len(audit_rows) == 1, "AtomicWriter audit evidence count differs")
    audit = audit_rows[0]
    require(audit.get("mechanism") == "atomicwriter" and
            audit.get("runtime_uid") == row.get("runtime_uid") and
            audit.get("runtime_gid") == row.get("runtime_gid") and
            audit.get("cleanup_root_absent") is True and
            audit.get("cleanup_root_remove_error") == 0 and
            audit.get("cleanup_root_stat_error") == 2 and
            audit.get("cleanup_parent_read_error") == 0 and
            audit.get("cleanup_parent_entries") == [] and
            audit.get("cleanup_pass") is True and audit.get("pass") is True,
            "AtomicWriter audit cleanup evidence differs")
    observations = audit.get("materialization", [])
    require([entry.get("state") for entry in observations] == list(STATES),
            "AtomicWriter materialization state sequence differs")
    require([entry.get("changed") for entry in observations] ==
            [True, True, False, True],
            "AtomicWriter generation-change sequence differs")
    for index, entry in enumerate(observations):
        require(entry.get("audit_lifecycle") is True and
                entry.get("live_regular_files") == width - 1 and
                entry.get("live_payload_bytes") == payload_bytes[index] and
                isinstance(entry.get("data_target"), str) and
                len(entry["data_target"]) > 0,
                "AtomicWriter live materialization inventory differs")
        if index == 2:
            require(entry.get("newly_materialized_files") == 0 and
                    entry.get("newly_materialized_bytes") == 0 and
                    entry.get("data_target") == observations[1].get("data_target"),
                    "AtomicWriter no-op unexpectedly materialized files")
        else:
            require(entry.get("newly_materialized_files") == width - 1 and
                    entry.get("newly_materialized_bytes") == payload_bytes[index] and
                    (index == 0 or entry.get("data_target") !=
                     observations[index - 1].get("data_target")),
                    "AtomicWriter changed generation materialization differs")


def sample_key(row):
    return row.get("boot"), row.get("pair"), row.get("width")


def validate_identity(row, rows):
    width = row["width"]
    uid = row["runtime_uid"]
    gid = row["runtime_gid"]
    v0, v1 = payloads(width)
    union = set(v0) | set(v1)
    require(len(rows) == 5 * width, "namei_ext identity evidence count differs")
    grouped = defaultdict(list)
    for item in rows:
        require(item.get("pass") is True, "failed identity evidence present")
        grouped[item.get("state")].append(item)
    require(set(grouped) == set(REPLAY_STATES) | {"unmanaged"},
            "namei_ext identity evidence state set differs")
    for index, state in enumerate(REPLAY_STATES):
        payload = v1 if index in (1, 2) else v0
        state_rows = grouped[state]
        require(len(state_rows) == width, f"identity count differs for {state}")
        by_path = {item.get("path"): item for item in state_rows}
        require(len(by_path) == width and set(by_path) == union,
                f"identity path set differs for {state}")
        for path in union:
            item = by_path[path]
            if path not in payload:
                require(item.get("expected_present") is False and
                        item.get("error") == 2 and
                        item.get("visible_dev") == 0 and
                        item.get("visible_ino") == 0,
                        f"hidden identity evidence differs for {state}:{path}")
                continue
            expected_bytes, expected_mode = payload[path]
            require(item.get("expected_present") is True and
                    item.get("error") == 0 and
                    item.get("expected_regular") is True and
                    item.get("visible_regular") is True and
                    item.get("expected_mode") == expected_mode and
                    item.get("visible_mode") == expected_mode and
                    item.get("expected_uid") == uid and
                    item.get("visible_uid") == uid and
                    item.get("expected_gid") == gid and
                    item.get("visible_gid") == gid and
                    item.get("expected_size") == len(expected_bytes) and
                    item.get("visible_size") == len(expected_bytes) and
                    item.get("expected_dev") == item.get("visible_dev") and
                    item.get("expected_ino") == item.get("visible_ino"),
                    f"selected identity evidence differs for {state}:{path}")
    unmanaged = grouped["unmanaged"]
    by_path = {item.get("path"): item for item in unmanaged}
    require(len(by_path) == width and set(by_path) == union,
            "unmanaged identity path set differs")
    for path, item in by_path.items():
        selected_v0 = (item.get("visible_dev"), item.get("visible_ino")) == (
            item.get("v0_dev"), item.get("v0_ino"))
        selected_v1 = (item.get("visible_dev"), item.get("visible_ino")) == (
            item.get("v1_dev"), item.get("v1_ino"))
        require(item.get("error") == 0 and item.get("visible_regular") is True and
                item.get("visible_mode") == 0o644 and
                item.get("visible_uid") == uid and item.get("visible_gid") == gid and
                item.get("visible_size") == 0 and not selected_v0 and not selected_v1,
                f"unmanaged placeholder identity differs for {path}")


def validate_lower(row, rows):
    width = row["width"]
    uid = row["runtime_uid"]
    gid = row["runtime_gid"]
    v0, v1 = payloads(width)
    expected = {(0, path): value for path, value in v0.items()}
    expected.update({(1, path): value for path, value in v1.items()})
    require(len(rows) == len(expected), "lower evidence count differs")
    by_path = {(item.get("generation"), item.get("path")): item
               for item in rows}
    require(len(by_path) == len(expected) and set(by_path) == set(expected),
            "lower evidence path set differs")
    for key, (expected_bytes, expected_mode) in expected.items():
        item = by_path[key]
        require(item.get("pass") is True and item.get("error") == 0 and
                item.get("expected_bytes") == expected_bytes and
                item.get("current_bytes") == expected_bytes and
                item.get("initial_regular") is True and
                item.get("current_regular") is True and
                item.get("initial_mode") == item.get("current_mode") and
                item.get("initial_mode", 0) & 0o777 == expected_mode and
                item.get("initial_uid") == item.get("current_uid") == uid and
                item.get("initial_gid") == item.get("current_gid") == gid and
                item.get("initial_dev") == item.get("current_dev") and
                item.get("initial_ino") == item.get("current_ino") and
                item.get("initial_size") == item.get("current_size") ==
                len(expected_bytes) and
                item.get("initial_mtime_sec") == item.get("current_mtime_sec") and
                item.get("initial_mtime_nsec") == item.get("current_mtime_nsec") and
                item.get("initial_ctime_sec") == item.get("current_ctime_sec") and
                item.get("initial_ctime_nsec") == item.get("current_ctime_nsec"),
                f"lower preservation evidence differs for {key}")


def validate_namei_target_links(row, identity, lower):
    targets = {
        (item["generation"], item["path"]):
        (item["initial_dev"], item["initial_ino"])
        for item in lower
    }
    for item in identity:
        path = item["path"]
        require((item.get("v0_dev"), item.get("v0_ino")) ==
                targets.get((0, path), (0, 0)) and
                (item.get("v1_dev"), item.get("v1_ino")) ==
                targets.get((1, path), (0, 0)),
                f"identity target IDs do not match lower evidence for {path}")
    for state_index, consumer in enumerate(row["consumer"]):
        generation = 1 if state_index in (1, 2) else 0
        for item in consumer["files"]:
            if item["error"] != 0:
                continue
            require((item["dev"], item["ino"]) ==
                    targets[(generation, item["path"])],
                    f"timed consumer identity does not match lower object for "
                    f"{consumer['state']}:{item['path']}")


def validate_unmanaged_directory(row, rows):
    v0, v1 = payloads(row["width"])
    root, config, tls = exact_names({**v0, **v1})
    require(len(rows) == 1, "unmanaged directory evidence count differs")
    item = rows[0]
    require(item.get("pass") is True and
            sorted(item.get("root_entries", [])) == root and
            sorted(item.get("config_entries", [])) == config and
            sorted(item.get("tls_entries", [])) == tls,
            "unmanaged directory view differs")


def validate_lifecycle(row):
    width = row["width"]
    # Recompute the phase aggregates used to explain the wall-span result.
    phases = row.get("phases", {})
    phase_names = {
        "setup_ns", "initial_publish_ns", "initial_consumer_ns",
        "update_publish_ns", "update_consumer_ns", "no_op_publish_ns",
        "no_op_consumer_ns", "rollback_publish_ns", "rollback_consumer_ns",
    }
    publication = sum(phases.get(name, -1) for name in (
        "initial_publish_ns", "update_publish_ns", "no_op_publish_ns",
        "rollback_publish_ns"))
    consumer = sum(phases.get(name, -1) for name in (
        "initial_consumer_ns", "update_consumer_ns", "no_op_consumer_ns",
        "rollback_consumer_ns"))
    active = phases.get("setup_ns", -1) + publication + consumer
    require(row.get("pass") is True and row.get("cleanup_pass") is True,
            "failed lifecycle row present")
    require(row.get("present_per_state") == width - 1 and
            row.get("changed_union_paths") == 4 and
            row.get("active_total_ns", 0) > 0 and
            row.get("wall_span_ns", 0) >= row.get("active_total_ns", 0) and
            set(phases) == phase_names and
            all(value > 0 for value in phases.values()) and
            row.get("publication_only_ns") == publication and
            row.get("consumer_only_ns") == consumer and
            row.get("active_total_ns") == active and
            row.get("publication_only_ns", 0) > 0 and
            row.get("consumer_only_ns", 0) > 0 and
            row.get("runtime_uid", 0) > 0 and row.get("runtime_gid", 0) > 0,
            "lifecycle dimensions or timing fields differ")
    if row.get("mechanism") == "namei_ext":
        setup = row.get("setup_phases", {})
        require(set(setup) == SETUP_PHASES and
                all(value > 0 for value in setup.values()) and
                sum(setup.values()) == phases["setup_ns"] and
                row.get("registered_targets") == 2 * (width - 1),
                "namei_ext setup decomposition differs")
    else:
        require("setup_phases" not in row and
                "registered_targets" not in row,
                "source row unexpectedly contains namei_ext setup evidence")
    validate_consumer(row)


def validate(run, rows):
    require(run.get("guest_launch") == {
        "kvm_cpus": 4,
        "kvm_memory": "8G",
        "host_cpu_pin": "4-7",
        "affinity": "qmp-pinned-and-verified",
    }, "guest launch protocol differs")
    require(run.get("filesystem") == {
        "type": "ext4",
        "layout": "fresh-virtio-block-per-boot",
        "image_format": "raw",
        "image_size": "1G",
        "host_backing_filesystem": "ext4",
        "qemu_cache": "none",
        "mkfs_options": [
            "-m", "0", "-E", "lazy_itable_init=0,lazy_journal_init=0"],
        "mount_options": ["noatime", "nosuid", "nodev"],
    }, "filesystem protocol differs")
    require(run.get("setup_measurement") == {
        "primary_metric": "wall_span_ns",
        "setup_aggregate": "setup_ns",
        "proposed_components": [
            "object_preparation_ns", "target_registration_ns",
            "map_population_ns", "consumer_cgroup_move_ns",
        ],
        "component_sum": "exact",
        "registered_targets": "2*(width-1)",
        "registration_control": "single-child-repeated-control-write",
    }, "setup measurement protocol differs")
    allowed = {
        LIFECYCLE, IDENTITY, LOWER, UNMANAGED_DIRECTORY,
        MATERIALIZATION_AUDIT,
    }
    require(all(row.get("event") in allowed for row in rows),
            "unexpected raw observation event")
    require(all(row.get("pass") is True for row in rows),
            "failed raw observation present")
    matrix = run["matrix"]
    boots = int(matrix["boots"])
    pairs = int(matrix["pairs_per_scale_per_boot"])
    scales = [int(value) for value in matrix["scales"]]
    lifecycle = [row for row in rows if row.get("event") == LIFECYCLE]
    require(len(lifecycle) == boots * pairs * len(scales) * 2,
            "lifecycle matrix is incomplete")
    grouped = defaultdict(list)
    for row in lifecycle:
        validate_lifecycle(row)
        require(row["width"] in scales, "undeclared lifecycle width")
        require(row["mechanism"] in ("atomicwriter", "namei_ext"),
                "undeclared mechanism")
        grouped[sample_key(row)].append(row)
    expected_keys = {
        (boot, pair, width)
        for boot in range(1, boots + 1)
        for width in scales
        for pair in range(1, pairs + 1)
    }
    require(set(grouped) == expected_keys,
            "lifecycle pair set is incomplete or outside the matrix")
    for boot in range(1, boots + 1):
        scale_order = [scales[(boot - 1 + offset) % len(scales)]
                       for offset in range(len(scales))]
        expected_order = []
        for width in scale_order:
            for pair in range(1, pairs + 1):
                first = ("atomicwriter" if (boot + pair) % 2 == 0
                         else "namei_ext")
                second = "namei_ext" if first == "atomicwriter" else "atomicwriter"
                expected_order.extend(((width, pair, 1, first),
                                       (width, pair, 2, second)))
        actual_order = [
            (row["width"], row["pair"], row["order"], row["mechanism"])
            for row in lifecycle if row["boot"] == boot
        ]
        require(actual_order == expected_order,
                f"condition or scale execution order differs for boot {boot}")
    identities = defaultdict(list)
    lowers = defaultdict(list)
    directories = defaultdict(list)
    audits = defaultdict(list)
    for item in rows:
        if item.get("event") == IDENTITY:
            identities[sample_key(item)].append(item)
        elif item.get("event") == LOWER:
            lowers[sample_key(item)].append(item)
        elif item.get("event") == UNMANAGED_DIRECTORY:
            directories[sample_key(item)].append(item)
        elif item.get("event") == MATERIALIZATION_AUDIT:
            audits[sample_key(item)].append(item)
    for key, pair_rows in grouped.items():
        require(sorted(row["mechanism"] for row in pair_rows) ==
                ["atomicwriter", "namei_ext"] and
                sorted(row["order"] for row in pair_rows) == [1, 2],
                f"unpaired mechanisms or invalid order for {key}")
        boot, pair, _ = key
        expected_first = "atomicwriter" if (boot + pair) % 2 == 0 else "namei_ext"
        first = next(item for item in pair_rows if item["order"] == 1)
        require(first["mechanism"] == expected_first,
                f"condition order is not counterbalanced for {key}")
        source = next(row for row in pair_rows
                      if row["mechanism"] == "atomicwriter")
        proposed = next(row for row in pair_rows
                        if row["mechanism"] == "namei_ext")
        require(source["runtime_uid"] == proposed["runtime_uid"] and
                source["runtime_gid"] == proposed["runtime_gid"],
                f"runtime identity differs within pair {key}")
        validate_atomicwriter(source, audits[key])
        v0, v1 = payloads(proposed["width"])
        exact_lower_bytes = sum(len(data) for payload in (v0, v1)
                                for data, _ in payload.values())
        counter_names = {"total", "lookup", "readdir", "select", "pass", "hide"}
        require(proposed.get("attach_ns", 0) > 0 and
                proposed.get("lower_files") == 2 * (proposed["width"] - 1) and
                proposed.get("lower_bytes") == exact_lower_bytes and
                proposed.get("observed_lower_files") == proposed.get("lower_files") and
                proposed.get("observed_lower_bytes") == proposed.get("lower_bytes") and
                proposed.get("logical_files") == proposed["width"] and
                proposed.get("managed_identity_checks") == 4 * (proposed["width"] - 1) and
                proposed.get("managed_hidden_checks") == 4 and
                proposed.get("lower_preservation_checks") ==
                2 * (proposed["width"] - 1) and
                proposed.get("unmanaged_checks") == proposed["width"] and
                proposed.get("rollback_original_v0") is True and
                proposed.get("unmanaged_scope_pass") is True and
                proposed.get("consumer_exit_status") == 0 and
                proposed.get("cleanup_generation_removed") is True and
                proposed.get("cleanup_view_maps_empty") is True and
                proposed.get("cleanup_policy_destroyed") is True and
                proposed.get("cleanup_targets_cleared") is True and
                proposed.get("cleanup_cgroup_removed") is True and
                proposed.get("cleanup_logical_removed") is True and
                proposed.get("cleanup_lower_removed") is True and
                proposed.get("cleanup_consumer_error") == 0 and
                proposed.get("cleanup_generation_error") == 0 and
                proposed.get("cleanup_map_error") == 0 and
                proposed.get("cleanup_v0_map_count") == 0 and
                proposed.get("cleanup_v1_map_count") == 0 and
                proposed.get("cleanup_policy_error") == 0 and
                proposed.get("cleanup_targets_error") == 0 and
                proposed.get("cleanup_cgroup_error") == 0 and
                proposed.get("cleanup_logical_lookup_error") == 2 and
                proposed.get("cleanup_lower_lookup_error") == 2 and
                set(proposed.get("counters", {})) == counter_names and
                all(value > 0 for value in proposed["counters"].values()),
                f"namei_ext lifecycle evidence differs for {key}")
        counters = proposed["counters"]
        require(counters["total"] == counters["lookup"] + counters["readdir"] and
                counters["total"] == counters["select"] + counters["pass"] +
                counters["hide"],
                f"namei_ext policy counter conservation differs for {key}")
        validate_identity(proposed, identities[key])
        validate_lower(proposed, lowers[key])
        validate_namei_target_links(proposed, identities[key], lowers[key])
        validate_unmanaged_directory(proposed, directories[key])
    require(set(identities) == set(grouped) and set(lowers) == set(grouped) and
            set(directories) == set(grouped) and set(audits) == set(grouped),
            "orphan or missing namei_ext evidence sample")


def validate_run(run_dir):
    run = load_json(run_dir / "run.json")
    rows = load_jsonl(run_dir / "observations.jsonl")
    validate(run, rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True, type=Path)
    arguments = parser.parse_args()
    validate_run(arguments.run_dir)


if __name__ == "__main__":
    main()

import copy
import unittest

import validate


UID = 1000
GID = 1000
WIDTH = 4


def consumer_ack(state, mechanism, root_ino, app_ino, first_app_ino):
    v0, v1 = validate.payloads(WIDTH)
    payload = v1 if state in ("update", "no-op") else v0
    union = set(v0) | set(v1)
    files = []
    generation = 1 if state in ("update", "no-op") else 0
    ordered_payload = sorted(payload)
    for index, path in enumerate(sorted(union)):
        if path in payload:
            data, mode = payload[path]
            if mechanism == "namei_ext":
                ino = 100 + generation * 20 + ordered_payload.index(path)
            else:
                materialization = {
                    "initial": 0, "update": 1, "no-op": 1, "rollback": 2,
                }[state]
                ino = (app_ino if path == "config/app.conf" else
                       50 + materialization * 20 + index)
            files.append({
                "path": path, "bytes": data, "error": 0, "mode": mode,
                "uid": UID, "gid": GID, "dev": 1,
                "ino": ino,
                "size": len(data),
            })
        else:
            files.append({
                "path": path, "bytes": "", "error": 2, "mode": 0,
                "uid": 0, "gid": 0, "dev": 0, "ino": 0, "size": 0,
            })
    root, config, tls = validate.exact_names(payload)
    initial = state == "initial"
    return {
        "state": state, "pass": True, "error": 0, "readdir_ops": 3,
        "open_ops": WIDTH if initial else WIDTH - 1,
        "read_ops": WIDTH - 1, "stat_ops": WIDTH + 1,
        "missing_ops": 1, "old_fd_ops": 0 if initial else 1,
        "visible_root_entries": WIDTH - 1, "root_dev": 1,
        "root_ino": root_ino, "app_dev": 1, "app_ino": app_ino,
        "old_dev": 1, "old_ino": first_app_ino,
        "old_bytes": "" if initial else "version=0\n",
        "old_error": 0, "old_mode": 0 if initial else 0o644,
        "old_uid": 0 if initial else UID, "old_gid": 0 if initial else GID,
        "old_size": 0 if initial else len("version=0\n"), "files": files,
        "root_entries": root, "config_entries": config, "tls_entries": tls,
    }


def lifecycle(mechanism, order):
    first = 10 if mechanism == "atomicwriter" else 100
    second = 20 if mechanism == "atomicwriter" else 121
    rollback = 30 if mechanism == "atomicwriter" else first
    row = {
        "event": validate.LIFECYCLE, "mechanism": mechanism,
        "boot": 1, "pair": 1, "order": order, "width": WIDTH,
        "present_per_state": WIDTH - 1, "changed_union_paths": 4,
        "active_total_ns": 100, "wall_span_ns": 120,
        "publication_only_ns": 40, "consumer_only_ns": 40,
        "phases": {
            "setup_ns": 20, "initial_publish_ns": 10,
            "update_publish_ns": 10, "no_op_publish_ns": 10,
            "rollback_publish_ns": 10, "initial_consumer_ns": 10,
            "update_consumer_ns": 10, "no_op_consumer_ns": 10,
            "rollback_consumer_ns": 10,
        },
        "runtime_uid": UID, "runtime_gid": GID,
        "cleanup_pass": True, "pass": True,
        "consumer": [
            consumer_ack("initial", mechanism, 2, first, first),
            consumer_ack("update", mechanism, 2, second, first),
            consumer_ack("no-op", mechanism, 2, second, first),
            consumer_ack("rollback", mechanism, 2, rollback, first),
        ],
    }
    if mechanism == "atomicwriter":
        row.update({
            "consumer_exit_status": 0, "cleanup_root_absent": True,
            "cleanup_parent_entries": [],
            "cleanup_root_remove_error": 0, "cleanup_root_stat_error": 2,
            "cleanup_parent_read_error": 0,
        })
    else:
        v0, v1 = validate.payloads(WIDTH)
        lower_bytes = sum(len(data) for payload in (v0, v1)
                          for data, _ in payload.values())
        row.update({
            "attach_ns": 10, "lower_files": 2 * (WIDTH - 1),
            "setup_phases": {
                "object_preparation_ns": 5,
                "target_registration_ns": 5,
                "map_population_ns": 5,
                "consumer_cgroup_move_ns": 5,
            },
            "registered_targets": 2 * (WIDTH - 1),
            "lower_bytes": lower_bytes,
            "observed_lower_files": 2 * (WIDTH - 1),
            "observed_lower_bytes": lower_bytes, "logical_files": WIDTH,
            "managed_identity_checks": 4 * (WIDTH - 1),
            "managed_hidden_checks": 4,
            "lower_preservation_checks": 2 * (WIDTH - 1),
            "unmanaged_checks": WIDTH, "rollback_original_v0": True,
            "unmanaged_scope_pass": True,
            "consumer_exit_status": 0,
            "cleanup_generation_removed": True,
            "cleanup_view_maps_empty": True,
            "cleanup_policy_destroyed": True,
            "cleanup_targets_cleared": True,
            "cleanup_cgroup_removed": True,
            "cleanup_logical_removed": True,
            "cleanup_lower_removed": True,
            "cleanup_consumer_error": 0,
            "cleanup_generation_error": 0,
            "cleanup_map_error": 0,
            "cleanup_v0_map_count": 0,
            "cleanup_v1_map_count": 0,
            "cleanup_policy_error": 0,
            "cleanup_targets_error": 0,
            "cleanup_cgroup_error": 0,
            "cleanup_logical_lookup_error": 2,
            "cleanup_lower_lookup_error": 2,
            "counters": {
                "total": 6, "lookup": 4, "readdir": 2,
                "select": 2, "pass": 3, "hide": 1,
            },
        })
    return row


def audit_row():
    v0, v1 = validate.payloads(WIDTH)
    payload_bytes = [sum(len(data) for data, _ in payload.values())
                     for payload in (v0, v1, v1, v0)]
    targets = ["..2026_01", "..2026_02", "..2026_02", "..2026_03"]
    materialization = [
        {"state": state, "changed": changed,
         "audit_lifecycle": True, "data_target": targets[index],
         "live_regular_files": WIDTH - 1,
         "live_payload_bytes": payload_bytes[index],
         "newly_materialized_files": WIDTH - 1 if changed else 0,
         "newly_materialized_bytes": payload_bytes[index] if changed else 0}
        for index, (state, changed) in enumerate(zip(
            validate.STATES, (True, True, False, True)))
    ]
    return {
        "event": validate.MATERIALIZATION_AUDIT, "mechanism": "atomicwriter",
        "boot": 1, "pair": 1, "width": WIDTH,
        "materialization": materialization, "runtime_uid": UID,
        "runtime_gid": GID, "cleanup_root_absent": True,
        "cleanup_parent_entries": [], "cleanup_root_remove_error": 0,
        "cleanup_root_stat_error": 2, "cleanup_parent_read_error": 0,
        "cleanup_pass": True, "pass": True,
    }


def identity_rows():
    v0, v1 = validate.payloads(WIDTH)
    union = sorted(set(v0) | set(v1))
    target_ids = {}
    for generation, payload in enumerate((v0, v1)):
        for index, path in enumerate(sorted(payload)):
            target_ids[(generation, path)] = (1, 100 + generation * 20 + index)
    rows = []
    for state_index, state in enumerate(validate.REPLAY_STATES):
        generation = 1 if state_index in (1, 2) else 0
        payload = v1 if generation else v0
        for path in union:
            present = path in payload
            data, mode = payload.get(path, ("", 0))
            dev, ino = target_ids.get((generation, path), (0, 0))
            rows.append({
                "event": validate.IDENTITY, "mechanism": "namei_ext",
                "boot": 1, "pair": 1, "width": WIDTH, "state": state,
                "path": path, "expected_present": present,
                "expected_dev": dev, "expected_ino": ino,
                "expected_mode": mode, "expected_uid": UID if present else 0,
                "expected_gid": GID if present else 0,
                "expected_size": len(data), "expected_regular": present,
                "visible_dev": dev, "visible_ino": ino,
                "visible_mode": mode, "visible_uid": UID if present else 0,
                "visible_gid": GID if present else 0,
                "visible_size": len(data), "visible_regular": present,
                "v0_dev": target_ids.get((0, path), (0, 0))[0],
                "v0_ino": target_ids.get((0, path), (0, 0))[1],
                "v1_dev": target_ids.get((1, path), (0, 0))[0],
                "v1_ino": target_ids.get((1, path), (0, 0))[1],
                "error": 0 if present else 2, "pass": True,
            })
    for index, path in enumerate(union):
        rows.append({
            "event": validate.IDENTITY, "mechanism": "namei_ext",
            "boot": 1, "pair": 1, "width": WIDTH, "state": "unmanaged",
            "path": path, "expected_present": True,
            "expected_dev": 0, "expected_ino": 0, "expected_mode": 0,
            "expected_uid": 0, "expected_gid": 0, "expected_size": 0,
            "expected_regular": False, "visible_dev": 1,
            "visible_ino": 500 + index, "visible_mode": 0o644,
            "visible_uid": UID, "visible_gid": GID, "visible_size": 0,
            "visible_regular": True,
            "v0_dev": target_ids.get((0, path), (0, 0))[0],
            "v0_ino": target_ids.get((0, path), (0, 0))[1],
            "v1_dev": target_ids.get((1, path), (0, 0))[0],
            "v1_ino": target_ids.get((1, path), (0, 0))[1],
            "error": 0, "pass": True,
        })
    return rows


def lower_rows():
    rows = []
    for generation, payload in enumerate(validate.payloads(WIDTH)):
        for index, (path, (data, mode)) in enumerate(sorted(payload.items())):
            rows.append({
                "event": validate.LOWER, "mechanism": "namei_ext",
                "boot": 1, "pair": 1, "width": WIDTH,
                "generation": generation, "path": path,
                "expected_bytes": data, "current_bytes": data,
                "initial_dev": 1, "current_dev": 1,
                "initial_ino": 100 + generation * 20 + index,
                "current_ino": 100 + generation * 20 + index,
                "initial_mode": 0o100000 | mode,
                "current_mode": 0o100000 | mode,
                "initial_uid": UID, "current_uid": UID,
                "initial_gid": GID, "current_gid": GID,
                "initial_size": len(data), "current_size": len(data),
                "initial_regular": True, "current_regular": True,
                "initial_mtime_sec": 1, "current_mtime_sec": 1,
                "initial_mtime_nsec": 2, "current_mtime_nsec": 2,
                "initial_ctime_sec": 3, "current_ctime_sec": 3,
                "initial_ctime_nsec": 4, "current_ctime_nsec": 4,
                "error": 0, "pass": True,
            })
    return rows


def fixture():
    source = lifecycle("atomicwriter", 1)
    proposed = lifecycle("namei_ext", 2)
    v0, v1 = validate.payloads(WIDTH)
    root, config, tls = validate.exact_names({**v0, **v1})
    directory = {
        "event": validate.UNMANAGED_DIRECTORY, "mechanism": "namei_ext",
        "boot": 1, "pair": 1, "width": WIDTH,
        "root_entries": root, "config_entries": config, "tls_entries": tls,
        "pass": True,
    }
    run = {
        "matrix": {"boots": 1, "pairs_per_scale_per_boot": 1,
                   "scales": [WIDTH]},
        "guest_launch": {
            "kvm_cpus": 4, "kvm_memory": "8G", "host_cpu_pin": "4-7",
            "affinity": "qmp-pinned-and-verified",
        },
        "filesystem": {
            "type": "ext4", "layout": "fresh-virtio-block-per-boot",
            "image_format": "raw", "image_size": "1G",
            "host_backing_filesystem": "ext4", "qemu_cache": "none",
            "mkfs_options": [
                "-m", "0", "-E", "lazy_itable_init=0,lazy_journal_init=0"],
            "mount_options": ["noatime", "nosuid", "nodev"],
        },
        "setup_measurement": {
            "primary_metric": "wall_span_ns",
            "setup_aggregate": "setup_ns",
            "proposed_components": [
                "object_preparation_ns", "target_registration_ns",
                "map_population_ns", "consumer_cgroup_move_ns",
            ],
            "component_sum": "exact",
            "registered_targets": "2*(width-1)",
            "registration_control": "single-child-repeated-control-write",
        },
    }
    return run, [source, proposed, *identity_rows(), *lower_rows(), directory,
                 audit_row()]


class ValidateTest(unittest.TestCase):
    def test_accepts_recomputable_source_and_namei_evidence(self):
        run, rows = fixture()
        validate.validate(run, rows)

    def test_rejects_consumer_byte_mismatch(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        file_row = next(item for item in broken[0]["consumer"][0]["files"]
                        if item["path"] == "config/app.conf")
        file_row["bytes"] = "wrong\n"
        with self.assertRaisesRegex(ValueError, "consumer bytes"):
            validate.validate(run, broken)

    def test_rejects_lower_timestamp_mismatch(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        lower = next(row for row in broken if row["event"] == validate.LOWER)
        lower["current_mtime_nsec"] += 1
        with self.assertRaisesRegex(ValueError, "lower preservation"):
            validate.validate(run, broken)

    def test_rejects_primary_timing_sum_mismatch(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        broken[0]["active_total_ns"] += 1
        with self.assertRaisesRegex(ValueError, "timing fields"):
            validate.validate(run, broken)

    def test_rejects_setup_decomposition_mismatch(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        namei = next(row for row in broken
                     if row["event"] == validate.LIFECYCLE and
                     row["mechanism"] == "namei_ext")
        namei["setup_phases"]["target_registration_ns"] += 1
        with self.assertRaisesRegex(ValueError, "setup decomposition"):
            validate.validate(run, broken)

    def test_rejects_registered_target_count_mismatch(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        namei = next(row for row in broken
                     if row["event"] == validate.LIFECYCLE and
                     row["mechanism"] == "namei_ext")
        namei["registered_targets"] -= 1
        with self.assertRaisesRegex(ValueError, "setup decomposition"):
            validate.validate(run, broken)

    def test_rejects_out_of_matrix_coordinate(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        for row in broken:
            row["boot"] = 2
        with self.assertRaisesRegex(ValueError, "outside the matrix"):
            validate.validate(run, broken)

    def test_rejects_summary_only_cleanup(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        namei = next(row for row in broken
                     if row["event"] == validate.LIFECYCLE and
                     row["mechanism"] == "namei_ext")
        namei["cleanup_view_maps_empty"] = False
        with self.assertRaisesRegex(ValueError, "lifecycle evidence"):
            validate.validate(run, broken)

    def test_rejects_non_app_noop_identity_change(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        source = broken[0]
        item = next(entry for entry in source["consumer"][2]["files"]
                    if entry["path"] == "tls/cert.pem")
        item["ino"] += 1
        with self.assertRaisesRegex(ValueError, "no-op changed selected object"):
            validate.validate(run, broken)

    def test_rejects_counter_conservation_mismatch(self):
        run, rows = fixture()
        broken = copy.deepcopy(rows)
        namei = next(row for row in broken
                     if row["event"] == validate.LIFECYCLE and
                     row["mechanism"] == "namei_ext")
        namei["counters"]["total"] += 1
        with self.assertRaisesRegex(ValueError, "counter conservation"):
            validate.validate(run, broken)

    def test_rejects_unfrozen_filesystem_protocol(self):
        run, rows = fixture()
        run["filesystem"]["type"] = "9p"
        with self.assertRaisesRegex(ValueError, "filesystem protocol"):
            validate.validate(run, rows)

    def test_rejects_unfrozen_setup_measurement(self):
        run, rows = fixture()
        run["setup_measurement"]["component_sum"] = "approximate"
        with self.assertRaisesRegex(ValueError, "setup measurement protocol"):
            validate.validate(run, rows)


if __name__ == "__main__":
    unittest.main()

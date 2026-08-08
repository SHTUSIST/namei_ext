#!/usr/bin/env python3
"""Host-baseline collector: what does one filesystem view cost to supply?

This measures the mount-based routes for supplying per-task path views on a
stock host kernel:

  control_process   N plain processes, no namespace and no mount. Subtracted
                    from mountns_bind so that the reported per-view cost is the
                    cost of the view, not the cost of the process holding it.
  mountns_bind      N processes, each in its own mount namespace, each with one
                    bind mount of a per-view tree onto a common pathname.
  overlayfs_mount   N overlayfs mounts, metacopy and redirect_dir enabled.
  btrfs_snapshot    N btrfs subvolume snapshots on a loop-backed image.

It also measures three things the mount route cannot avoid:

  * how long it takes to create one further mount namespace once N views
    already exist, because creating a mount namespace copies the whole mount
    table;
  * how much page cache N views of the same content occupy;
  * whether a view can be re-pointed while a process is still using it.

namei_ext itself is NOT measured here. It needs the patched kernel in KVM, so
its column is recorded as not measured rather than estimated. This target is
explicitly not Phase 1 validation.

Raw observations only. Ratios, percentiles and confidence intervals are
computed by analysis/view_supply_cost_host/analyze.py.
"""

import argparse
import ctypes
import ctypes.util
import errno
import json
import os
import shutil
import signal
import subprocess
import sys
import time

CLONE_NEWNS = 0x00020000
MS_BIND = 0x1000
MS_REC = 0x4000
MS_PRIVATE = 1 << 18

_LIBC = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)


def fail(message):
    raise SystemExit("view_supply_cost_host: %s" % message)


def libc_unshare(flags):
    if _LIBC.unshare(ctypes.c_int(flags)) != 0:
        err = ctypes.get_errno()
        fail("unshare(%#x) failed: %s" % (flags, os.strerror(err)))


def libc_mount(source, target, fstype, flags, data):
    def enc(value):
        return None if value is None else value.encode()

    rc = _LIBC.mount(enc(source), enc(target), enc(fstype),
                     ctypes.c_ulong(flags), enc(data))
    if rc != 0:
        err = ctypes.get_errno()
        fail("mount(%s -> %s, %s, %s) failed: %s"
             % (source, target, fstype, data, os.strerror(err)))


def read_meminfo():
    values = {}
    with open("/proc/meminfo", "r") as handle:
        for line in handle:
            key, _, rest = line.partition(":")
            parts = rest.split()
            if parts:
                values[key] = int(parts[0])  # kB
    return values


def memory_sample():
    info = read_meminfo()
    return {
        "mem_available_kb": info["MemAvailable"],
        "mem_free_kb": info["MemFree"],
        "slab_kb": info["Slab"],
        "slab_unreclaim_kb": info["SUnreclaim"],
        "slab_reclaimable_kb": info["SReclaimable"],
        "cached_kb": info["Cached"],
        "page_tables_kb": info.get("PageTables", 0),
    }


def mountinfo_lines():
    with open("/proc/self/mountinfo", "r") as handle:
        return sum(1 for line in handle if line.strip())


def guard_memory(guard_kb, where):
    available = read_meminfo()["MemAvailable"]
    if available < guard_kb:
        fail("memory guard tripped at %s: MemAvailable %d kB below floor %d kB"
             % (where, available, guard_kb))
    return available


def drop_caches():
    os.sync()
    with open("/proc/sys/vm/drop_caches", "w") as handle:
        handle.write("3\n")
    time.sleep(0.4)


def make_content_tree(root, files, file_bytes):
    """A small tree that stands in for one view's content."""
    os.makedirs(root, exist_ok=True)
    payload = os.urandom(file_bytes)
    for index in range(files):
        directory = os.path.join(root, "d%02d" % (index % 8))
        os.makedirs(directory, exist_ok=True)
        with open(os.path.join(directory, "f%04d" % index), "wb") as handle:
            handle.write(payload)
    return root


def read_tree(root):
    total = 0
    for directory, _, names in os.walk(root):
        for name in names:
            with open(os.path.join(directory, name), "rb") as handle:
                while True:
                    chunk = handle.read(1 << 20)
                    if not chunk:
                        break
                    total += len(chunk)
    return total


# --------------------------------------------------------------------------
# Part A: per-view supply cost and resident cost
# --------------------------------------------------------------------------

class HolderFleet:
    def __init__(self, binary, mode, source_dir, view_root):
        self.binary = binary
        self.mode = mode
        self.source_dir = source_dir
        self.view_root = view_root
        self.procs = []

    def spawn_one(self, index):
        view_path = os.path.join(self.view_root, "v%06d" % index)
        os.makedirs(view_path, exist_ok=True)
        proc = subprocess.Popen(
            [self.binary, self.mode, self.source_dir, view_path],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        line = proc.stdout.readline()
        if line != b"ready\n":
            stderr = proc.stderr.read().decode(errors="replace")
            proc.kill()
            fail("holder %d did not become ready: %s" % (index, stderr.strip()))
        self.procs.append(proc)
        return proc

    def teardown(self):
        for proc in self.procs:
            if proc.poll() is None:
                proc.send_signal(signal.SIGKILL)
        for proc in self.procs:
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                fail("holder did not exit within 30s")
            for stream in (proc.stdout, proc.stderr):
                if stream is not None:
                    stream.close()
        self.procs = []


def run_process_condition(condition, count, ctx):
    fleet = HolderFleet(ctx["holder_binary"],
                        "control" if condition == "control_process" else "mountns_bind",
                        ctx["source_dir"], ctx["view_root"])
    before = memory_sample()
    mounts_before = mountinfo_lines()
    supply_ns = []
    try:
        for index in range(count):
            if index % 100 == 0:
                guard_memory(ctx["memory_guard_kb"],
                             "%s n=%d i=%d" % (condition, count, index))
            start = time.perf_counter_ns()
            fleet.spawn_one(index)
            supply_ns.append(time.perf_counter_ns() - start)
        time.sleep(0.5)
        after = memory_sample()
        mounts_after = mountinfo_lines()
        clone_ns = measure_mountns_clone(ctx["mountns_clone_reps"])
    finally:
        fleet.teardown()
    return {
        "supply_ns": supply_ns,
        "memory_before": before,
        "memory_after": after,
        "host_mountinfo_lines_before": mounts_before,
        "host_mountinfo_lines_after": mounts_after,
        "mountns_clone_ns": clone_ns,
    }


def run_overlayfs_condition(count, ctx):
    base = ctx["overlay_root"]
    lower = ctx["source_dir"]
    before = memory_sample()
    mounts_before = mountinfo_lines()
    supply_ns = []
    mounted = []
    try:
        for index in range(count):
            if index % 100 == 0:
                guard_memory(ctx["memory_guard_kb"],
                             "overlayfs_mount n=%d i=%d" % (count, index))
            upper = os.path.join(base, "u%06d" % index)
            work = os.path.join(base, "w%06d" % index)
            merged = os.path.join(base, "m%06d" % index)
            for path in (upper, work, merged):
                os.makedirs(path, exist_ok=True)
            options = ("lowerdir=%s,upperdir=%s,workdir=%s,"
                       "metacopy=on,redirect_dir=on" % (lower, upper, work))
            start = time.perf_counter_ns()
            libc_mount("overlay", merged, "overlay", 0, options)
            supply_ns.append(time.perf_counter_ns() - start)
            mounted.append(merged)
        time.sleep(0.5)
        after = memory_sample()
        mounts_after = mountinfo_lines()
        clone_ns = measure_mountns_clone(ctx["mountns_clone_reps"])
    finally:
        for merged in reversed(mounted):
            subprocess.run(["umount", "-l", merged], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {
        "supply_ns": supply_ns,
        "memory_before": before,
        "memory_after": after,
        "host_mountinfo_lines_before": mounts_before,
        "host_mountinfo_lines_after": mounts_after,
        "mountns_clone_ns": clone_ns,
        "mount_options": "lowerdir=<shared>,upperdir=<per-view>,workdir=<per-view>,metacopy=on,redirect_dir=on",
    }


def run_btrfs_condition(count, ctx):
    mountpoint = ctx["btrfs_mountpoint"]
    origin = os.path.join(mountpoint, "origin")
    before = memory_sample()
    mounts_before = mountinfo_lines()
    supply_ns = []
    created = []
    try:
        for index in range(count):
            if index % 100 == 0:
                guard_memory(ctx["memory_guard_kb"],
                             "btrfs_snapshot n=%d i=%d" % (count, index))
            snapshot = os.path.join(mountpoint, "snap%06d" % index)
            start = time.perf_counter_ns()
            proc = subprocess.run(["btrfs", "subvolume", "snapshot",
                                   origin, snapshot],
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
            elapsed = time.perf_counter_ns() - start
            if proc.returncode != 0:
                fail("btrfs snapshot %d failed: %s"
                     % (index, proc.stderr.decode(errors="replace").strip()))
            supply_ns.append(elapsed)
            created.append(snapshot)
        time.sleep(0.5)
        after = memory_sample()
        mounts_after = mountinfo_lines()
        clone_ns = measure_mountns_clone(ctx["mountns_clone_reps"])
    finally:
        for snapshot in reversed(created):
            subprocess.run(["btrfs", "subvolume", "delete", snapshot],
                           check=False, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    return {
        "supply_ns": supply_ns,
        "memory_before": before,
        "memory_after": after,
        "host_mountinfo_lines_before": mounts_before,
        "host_mountinfo_lines_after": mounts_after,
        "mountns_clone_ns": clone_ns,
    }


# --------------------------------------------------------------------------
# Part B: cost of creating one more mount namespace once N views exist
# --------------------------------------------------------------------------

def measure_mountns_clone(repetitions):
    """Time a fork+unshare(CLONE_NEWNS) round trip in the current mount table.

    Creating a mount namespace copies the caller's whole mount table, so this
    is where a mount-per-view route pays for the views it already supplied.
    """
    samples = []
    for _ in range(repetitions):
        read_fd, write_fd = os.pipe()
        start = time.perf_counter_ns()
        pid = os.fork()
        if pid == 0:
            try:
                os.close(read_fd)
                _LIBC.unshare(ctypes.c_int(CLONE_NEWNS))
                os.write(write_fd, b"x")
            finally:
                os._exit(0)
        os.close(write_fd)
        got = os.read(read_fd, 1)
        elapsed = time.perf_counter_ns() - start
        os.close(read_fd)
        os.waitpid(pid, 0)
        if got != b"x":
            fail("mount namespace clone probe produced no signal")
        samples.append(elapsed)
    return samples


# --------------------------------------------------------------------------
# Part C: page cache occupied by N views of the same content
# --------------------------------------------------------------------------

def run_page_cache_probe(count, ctx):
    """Read the same logical content through N views and measure page cache."""
    results = {}

    # bind: N pathnames, one underlying tree.
    base = os.path.join(ctx["pagecache_root"], "bind")
    os.makedirs(base, exist_ok=True)
    targets = []
    drop_caches()
    before = memory_sample()
    for index in range(count):
        target = os.path.join(base, "v%04d" % index)
        os.makedirs(target, exist_ok=True)
        libc_mount(ctx["source_dir"], target, None, MS_BIND | MS_REC, None)
        targets.append(target)
    read_bytes = sum(read_tree(target) for target in targets)
    after = memory_sample()
    for target in reversed(targets):
        subprocess.run(["umount", "-l", target], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    results["bind_same_source"] = {
        "views": count, "bytes_read": read_bytes,
        "cached_before_kb": before["cached_kb"],
        "cached_after_kb": after["cached_kb"],
    }

    # overlay: N overlay mounts, one shared lowerdir.
    base = os.path.join(ctx["pagecache_root"], "overlay")
    os.makedirs(base, exist_ok=True)
    merged_dirs = []
    drop_caches()
    before = memory_sample()
    for index in range(count):
        upper = os.path.join(base, "u%04d" % index)
        work = os.path.join(base, "w%04d" % index)
        merged = os.path.join(base, "m%04d" % index)
        for path in (upper, work, merged):
            os.makedirs(path, exist_ok=True)
        libc_mount("overlay", merged, "overlay", 0,
                   "lowerdir=%s,upperdir=%s,workdir=%s,metacopy=on,redirect_dir=on"
                   % (ctx["source_dir"], upper, work))
        merged_dirs.append(merged)
    read_bytes = sum(read_tree(merged) for merged in merged_dirs)
    after = memory_sample()
    for merged in reversed(merged_dirs):
        subprocess.run(["umount", "-l", merged], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    results["overlay_same_lower"] = {
        "views": count, "bytes_read": read_bytes,
        "cached_before_kb": before["cached_kb"],
        "cached_after_kb": after["cached_kb"],
    }

    # btrfs: N snapshots of the same subvolume; each snapshot has its own
    # inodes even though the extents are shared on disk.
    mountpoint = ctx["btrfs_mountpoint"]
    snapshots = []
    drop_caches()
    before = memory_sample()
    for index in range(count):
        snapshot = os.path.join(mountpoint, "pc%04d" % index)
        proc = subprocess.run(["btrfs", "subvolume", "snapshot",
                               os.path.join(mountpoint, "origin"), snapshot],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            fail("btrfs page-cache snapshot failed: %s"
                 % proc.stderr.decode(errors="replace").strip())
        snapshots.append(snapshot)
    read_bytes = sum(read_tree(snapshot) for snapshot in snapshots)
    after = memory_sample()
    for snapshot in reversed(snapshots):
        subprocess.run(["btrfs", "subvolume", "delete", snapshot], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    results["btrfs_snapshot"] = {
        "views": count, "bytes_read": read_bytes,
        "cached_before_kb": before["cached_kb"],
        "cached_after_kb": after["cached_kb"],
    }
    return results


# --------------------------------------------------------------------------
# Part D: re-pointing a view that is still in use
# --------------------------------------------------------------------------

def run_switch_probe(ctx):
    """Re-point one view from tree A to tree B and record what it costs.

    Two situations are reported separately, because the mount route can only
    reach the second one by first evicting the user of the view:
      idle      nothing is using the view
      in_use    a process has its working directory inside the view
    """
    base = os.path.join(ctx["switch_root"])
    os.makedirs(base, exist_ok=True)
    view = os.path.join(base, "view")
    os.makedirs(view, exist_ok=True)
    tree_a = make_content_tree(os.path.join(base, "a"), 4, 4096)
    tree_b = make_content_tree(os.path.join(base, "b"), 4, 4096)
    with open(os.path.join(tree_a, "marker"), "w") as handle:
        handle.write("A")
    with open(os.path.join(tree_b, "marker"), "w") as handle:
        handle.write("B")

    observations = []

    # idle switch: umount + mount
    libc_mount(tree_a, view, None, MS_BIND | MS_REC, None)
    idle_samples = []
    for _ in range(ctx["switch_reps"]):
        start = time.perf_counter_ns()
        proc = subprocess.run(["umount", view], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
        if proc.returncode != 0:
            fail("idle umount failed: %s"
                 % proc.stderr.decode(errors="replace").strip())
        libc_mount(tree_b, view, None, MS_BIND | MS_REC, None)
        idle_samples.append(time.perf_counter_ns() - start)
        proc = subprocess.run(["umount", view], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
        if proc.returncode != 0:
            fail("idle umount (restore) failed")
        libc_mount(tree_a, view, None, MS_BIND | MS_REC, None)
    observations.append({"situation": "idle", "switch_ns": idle_samples})

    # in-use switch: a process keeps its working directory inside the view
    holder = subprocess.Popen(["sleep", "600"], cwd=view)
    time.sleep(0.3)
    proc = subprocess.run(["umount", view], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    observations.append({
        "situation": "in_use",
        "umount_returncode": proc.returncode,
        "umount_stderr": proc.stderr.decode(errors="replace").strip(),
    })
    holder.kill()
    holder.wait(timeout=30)
    subprocess.run(["umount", "-l", view], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return observations


# --------------------------------------------------------------------------
# Orchestration
# --------------------------------------------------------------------------

def setup_btrfs(ctx):
    image = ctx["btrfs_image"]
    mountpoint = ctx["btrfs_mountpoint"]
    os.makedirs(os.path.dirname(image), exist_ok=True)
    os.makedirs(mountpoint, exist_ok=True)
    with open(image, "wb") as handle:
        handle.truncate(ctx["btrfs_image_bytes"])
    proc = subprocess.run(["mkfs.btrfs", "-q", "-f", image],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        fail("mkfs.btrfs failed: %s"
             % proc.stderr.decode(errors="replace").strip())
    proc = subprocess.run(["mount", "-o", "loop", image, mountpoint],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        fail("mounting btrfs image failed: %s"
             % proc.stderr.decode(errors="replace").strip())
    origin = os.path.join(mountpoint, "origin")
    proc = subprocess.run(["btrfs", "subvolume", "create", origin],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        fail("btrfs subvolume create failed: %s"
             % proc.stderr.decode(errors="replace").strip())
    make_content_tree(origin, ctx["content_files"], ctx["content_file_bytes"])
    # Quota groups make snapshot creation much slower at scale; the setting is
    # frozen here and recorded so the numbers are not mixed with a quota run.
    proc = subprocess.run(["btrfs", "qgroup", "show", mountpoint],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ctx["btrfs_qgroup_state"] = (
        "enabled" if proc.returncode == 0 else "disabled")


def teardown_btrfs(ctx):
    subprocess.run(["umount", "-l", ctx["btrfs_mountpoint"]], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if os.path.exists(ctx["btrfs_image"]):
        os.unlink(ctx["btrfs_image"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--result-dir", required=True)
    parser.add_argument("--scratch-dir", required=True)
    parser.add_argument("--holder-binary", required=True)
    parser.add_argument("--ladder", default="1,10,100,1000")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--mountns-clone-reps", type=int, default=30)
    parser.add_argument("--switch-reps", type=int, default=30)
    parser.add_argument("--page-cache-views", type=int, default=32)
    parser.add_argument("--content-files", type=int, default=64)
    parser.add_argument("--content-file-bytes", type=int, default=262144)
    parser.add_argument("--btrfs-image-bytes", type=int, default=2 << 30)
    parser.add_argument("--memory-guard-mb", type=int, default=3072)
    args = parser.parse_args()

    if os.geteuid() != 0:
        fail("this collector needs root for mount and namespace operations")

    ladder = [int(value) for value in args.ladder.split(",") if value]
    scratch = args.scratch_dir
    if os.path.exists(scratch):
        shutil.rmtree(scratch)
    os.makedirs(scratch)
    os.makedirs(args.result_dir, exist_ok=False)

    ctx = {
        "holder_binary": args.holder_binary,
        "source_dir": os.path.join(scratch, "source"),
        "view_root": os.path.join(scratch, "views"),
        "overlay_root": os.path.join(scratch, "overlay"),
        "pagecache_root": os.path.join(scratch, "pagecache"),
        "switch_root": os.path.join(scratch, "switch"),
        "btrfs_image": os.path.join(scratch, "btrfs.img"),
        "btrfs_mountpoint": os.path.join(scratch, "btrfs"),
        "btrfs_image_bytes": args.btrfs_image_bytes,
        "content_files": args.content_files,
        "content_file_bytes": args.content_file_bytes,
        "memory_guard_kb": args.memory_guard_mb * 1024,
        "mountns_clone_reps": args.mountns_clone_reps,
        "switch_reps": args.switch_reps,
    }
    for key in ("view_root", "overlay_root", "pagecache_root", "switch_root"):
        os.makedirs(ctx[key], exist_ok=True)
    make_content_tree(ctx["source_dir"], args.content_files,
                      args.content_file_bytes)

    # Isolate every mount this collector makes from the rest of the machine.
    libc_unshare(CLONE_NEWNS)
    libc_mount(None, "/", None, MS_REC | MS_PRIVATE, None)

    observations_path = os.path.join(args.result_dir, "observations.jsonl")
    started_at = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

    setup_btrfs(ctx)
    try:
        with open(observations_path, "w") as sink:
            def emit(record):
                sink.write(json.dumps(record, sort_keys=True) + "\n")
                sink.flush()

            emit({"kind": "environment",
                  "uname": os.uname()._asdict() if hasattr(os.uname(), "_asdict")
                          else list(os.uname()),
                  "meminfo": memory_sample(),
                  "host_mountinfo_lines": mountinfo_lines(),
                  "btrfs_qgroup_state": ctx.get("btrfs_qgroup_state"),
                  "content_files": args.content_files,
                  "content_file_bytes": args.content_file_bytes,
                  "started_at": started_at})

            for trial in range(args.trials):
                for count in ladder:
                    for condition in ("control_process", "mountns_bind",
                                      "overlayfs_mount", "btrfs_snapshot"):
                        guard_memory(ctx["memory_guard_kb"],
                                     "before %s n=%d" % (condition, count))
                        if condition in ("control_process", "mountns_bind"):
                            payload = run_process_condition(condition, count, ctx)
                        elif condition == "overlayfs_mount":
                            payload = run_overlayfs_condition(count, ctx)
                        else:
                            payload = run_btrfs_condition(count, ctx)
                        payload.update({"kind": "supply",
                                        "condition": condition,
                                        "views": count,
                                        "trial": trial})
                        emit(payload)
                        time.sleep(0.5)

            for trial in range(args.trials):
                emit({"kind": "page_cache", "trial": trial,
                      "results": run_page_cache_probe(args.page_cache_views, ctx)})

            for trial in range(args.trials):
                emit({"kind": "switch", "trial": trial,
                      "observations": run_switch_probe(ctx)})

            emit({"kind": "namei_ext_column",
                  "measured": False,
                  "reason": "namei_ext needs the patched kernel in KVM; this "
                            "host-baseline target deliberately does not "
                            "estimate it"})
            emit({"kind": "completion",
                  "finished_at": time.strftime("%Y-%m-%dT%H:%M:%SZ",
                                               time.gmtime()),
                  "ladder": ladder, "trials": args.trials})
    finally:
        teardown_btrfs(ctx)
        shutil.rmtree(scratch, ignore_errors=True)

    print(observations_path)


if __name__ == "__main__":
    main()

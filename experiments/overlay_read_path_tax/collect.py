#!/usr/bin/env python3
"""Host-baseline collector: what does a write layer cost the read path?

overlayfs supplies write isolation by putting a writable upper directory on top
of one or more read-only lower directories, fixed at mount time. Every path
resolution then goes through the overlay, whether or not the operation is going
to write anything. This collector measures what that costs, as a function of
how deep the layer stack is:

  native        the lower filesystem directly, no overlay
  overlay_dN    one writable upper over N read-only lower directories, with
                metacopy and redirect_dir enabled; the content lives in the
                bottom lower directory, so a lookup has to traverse the stack

Measured per condition:
  stat_ns       warm path resolution only (the file is already in cache)
  open_ns       warm open + close
  cold_read_ns  drop caches, then open + read the whole file
  readdir_ns    enumerate a directory that exists in every layer
  identity      st_dev / st_ino as seen through the view, plus what
                /proc/self/fd and realpath report, because a tool that
                canonicalises a path can observe the difference

namei_ext is not measured here: it needs the patched kernel in KVM. The point of
this collector is the shape of the overlayfs curve, which is what a
lookup-time binding decision would avoid on read-only operations.

Raw observations only. Percentiles and confidence intervals belong to
analysis/overlay_read_path_tax/analyze.py.
"""

import argparse
import ctypes
import ctypes.util
import json
import os
import random
import shutil
import subprocess
import time

CLONE_NEWNS = 0x00020000
MS_REC = 0x4000
MS_PRIVATE = 1 << 18

_LIBC = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)


def fail(message):
    raise SystemExit("overlay_read_path_tax: %s" % message)


def libc_unshare(flags):
    if _LIBC.unshare(ctypes.c_int(flags)) != 0:
        fail("unshare failed: %s" % os.strerror(ctypes.get_errno()))


def libc_mount(source, target, fstype, flags, data):
    def enc(value):
        return None if value is None else value.encode()
    if _LIBC.mount(enc(source), enc(target), enc(fstype),
                   ctypes.c_ulong(flags), enc(data)) != 0:
        fail("mount(%s -> %s, %s) failed: %s"
             % (source, target, data, os.strerror(ctypes.get_errno())))


def drop_caches():
    os.sync()
    with open("/proc/sys/vm/drop_caches", "w") as handle:
        handle.write("3\n")
    time.sleep(0.3)


def build_content(root, files, dirs, file_bytes):
    payload = os.urandom(file_bytes)
    paths = []
    for index in range(files):
        directory = os.path.join(root, "d%03d" % (index % dirs))
        os.makedirs(directory, exist_ok=True)
        path = os.path.join(directory, "f%05d" % index)
        with open(path, "wb") as handle:
            handle.write(payload)
        paths.append(os.path.relpath(path, root))
    return paths


def measure_stat(view, relatives, repetitions, rng):
    samples = []
    for _ in range(repetitions):
        target = os.path.join(view, rng.choice(relatives))
        start = time.perf_counter_ns()
        os.stat(target)
        samples.append(time.perf_counter_ns() - start)
    return samples


def measure_open(view, relatives, repetitions, rng):
    samples = []
    for _ in range(repetitions):
        target = os.path.join(view, rng.choice(relatives))
        start = time.perf_counter_ns()
        fd = os.open(target, os.O_RDONLY)
        os.close(fd)
        samples.append(time.perf_counter_ns() - start)
    return samples


def measure_cold_read(view, relatives, repetitions, rng):
    samples = []
    for _ in range(repetitions):
        target = os.path.join(view, rng.choice(relatives))
        drop_caches()
        start = time.perf_counter_ns()
        with open(target, "rb") as handle:
            while handle.read(1 << 20):
                pass
        samples.append(time.perf_counter_ns() - start)
    return samples


def measure_readdir(view, directory, repetitions):
    samples = []
    target = os.path.join(view, directory)
    for _ in range(repetitions):
        start = time.perf_counter_ns()
        entries = len(os.listdir(target))
        elapsed = time.perf_counter_ns() - start
        if entries == 0:
            fail("readdir returned no entries for %s" % target)
        samples.append(elapsed)
    return samples


def observe_identity(view, relative):
    target = os.path.join(view, relative)
    info = os.stat(target)
    fd = os.open(target, os.O_RDONLY)
    try:
        link = os.readlink("/proc/self/fd/%d" % fd)
    finally:
        os.close(fd)
    return {
        "path": target,
        "st_dev": info.st_dev,
        "st_ino": info.st_ino,
        "realpath": os.path.realpath(target),
        "proc_self_fd": link,
    }


def mount_overlay(base, depth, content_root, view):
    """Upper over `depth` lower directories, content in the bottom lower.

    The whole mount option string has to fit in one page, so a deep stack
    written with long absolute paths simply cannot be expressed: the mount
    fails with ENOENT. That limit is itself worth recording, so the scratch
    paths here are kept short deliberately.
    """
    lowers = []
    for level in range(depth):
        if level == depth - 1:
            lowers.append(content_root)
        else:
            empty = os.path.join(base, "l%02d" % level)
            os.makedirs(empty, exist_ok=True)
            # A directory that exists in every layer forces the merge path.
            os.makedirs(os.path.join(empty, "d000"), exist_ok=True)
            lowers.append(empty)
    upper = os.path.join(base, "up")
    work = os.path.join(base, "wk")
    for path in (upper, work, view):
        os.makedirs(path, exist_ok=True)
    options = ("lowerdir=%s,upperdir=%s,workdir=%s,metacopy=on,redirect_dir=on"
               % (":".join(lowers), upper, work))
    if len(options) >= 4096:
        fail("mount option string is %d bytes and cannot fit in one page; "
             "shorten --scratch-dir" % len(options))
    libc_mount("overlay", view, "overlay", 0, options)
    return options


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--result-dir", required=True)
    parser.add_argument("--scratch-dir", required=True)
    parser.add_argument("--depths", default="1,2,5,10,25,50")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--warm-reps", type=int, default=20000)
    parser.add_argument("--cold-reps", type=int, default=30)
    parser.add_argument("--readdir-reps", type=int, default=200)
    parser.add_argument("--files", type=int, default=2000)
    parser.add_argument("--dirs", type=int, default=20)
    parser.add_argument("--file-bytes", type=int, default=65536)
    parser.add_argument("--seed", type=int, default=20260808)
    args = parser.parse_args()

    if os.geteuid() != 0:
        fail("needs root for mount operations")

    scratch = args.scratch_dir
    if os.path.exists(scratch):
        shutil.rmtree(scratch)
    os.makedirs(scratch)
    os.makedirs(args.result_dir, exist_ok=False)

    content_root = os.path.join(scratch, "content")
    os.makedirs(content_root)
    relatives = build_content(content_root, args.files, args.dirs,
                              args.file_bytes)

    libc_unshare(CLONE_NEWNS)
    libc_mount(None, "/", None, MS_REC | MS_PRIVATE, None)

    depths = [int(value) for value in args.depths.split(",") if value]
    observations_path = os.path.join(args.result_dir, "observations.jsonl")

    with open(observations_path, "w") as sink:
        def emit(record):
            sink.write(json.dumps(record, sort_keys=True) + "\n")
            sink.flush()

        emit({"kind": "environment",
              "uname": list(os.uname()),
              "files": args.files, "dirs": args.dirs,
              "file_bytes": args.file_bytes,
              "warm_reps": args.warm_reps, "cold_reps": args.cold_reps,
              "readdir_reps": args.readdir_reps,
              "depths": depths, "trials": args.trials,
              "seed": args.seed,
              "started_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})

        for trial in range(args.trials):
            rng = random.Random(args.seed + trial)

            # native
            emit({"kind": "read_path", "condition": "native", "depth": 0,
                  "trial": trial,
                  "mount_options": None,
                  "identity": observe_identity(content_root, relatives[0]),
                  "stat_ns": measure_stat(content_root, relatives,
                                          args.warm_reps, rng),
                  "open_ns": measure_open(content_root, relatives,
                                          args.warm_reps, rng),
                  "readdir_ns": measure_readdir(content_root, "d000",
                                                args.readdir_reps),
                  "cold_read_ns": measure_cold_read(content_root, relatives,
                                                    args.cold_reps, rng)})

            for depth in depths:
                base = os.path.join(scratch, "s%d_%d" % (trial, depth))
                view = os.path.join(base, "v")
                os.makedirs(base, exist_ok=True)
                options = mount_overlay(base, depth, content_root, view)
                try:
                    rng = random.Random(args.seed + trial)
                    emit({"kind": "read_path",
                          "condition": "overlay_d%d" % depth,
                          "depth": depth, "trial": trial,
                          "mount_options": options,
                          "identity": observe_identity(view, relatives[0]),
                          "stat_ns": measure_stat(view, relatives,
                                                  args.warm_reps, rng),
                          "open_ns": measure_open(view, relatives,
                                                  args.warm_reps, rng),
                          "readdir_ns": measure_readdir(view, "d000",
                                                        args.readdir_reps),
                          "cold_read_ns": measure_cold_read(view, relatives,
                                                            args.cold_reps,
                                                            rng)})
                finally:
                    subprocess.run(["umount", "-l", view], check=False,
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
                    shutil.rmtree(base, ignore_errors=True)

        emit({"kind": "completion",
              "finished_at": time.strftime("%Y-%m-%dT%H:%M:%SZ",
                                           time.gmtime())})

    shutil.rmtree(scratch, ignore_errors=True)
    print(observations_path)


if __name__ == "__main__":
    main()

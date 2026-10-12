import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import time


CGROUP_ROOT = Path("/sys/fs/cgroup")
PROC_ROOT = Path("/proc")
COMPILER = re.compile(r"^(clang(?:\+\+)?|cc1|cc1plus)(?:-\d+)?$")


def cpu_percent(value):
    quota, period = value.split()
    period = int(period)
    if period <= 0:
        raise ValueError("Nonpositive quota period")
    if quota == "max":
        return None
    quota = int(quota)
    if quota <= 0:
        raise ValueError("Nonpositive CPU quota")
    return 100 * quota / period


def file_hash(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def changed_inputs(expected):
    changed = []
    for path, digest in expected.items():
        try:
            if file_hash(path) == digest:
                continue
        except OSError:
            pass
        changed.append(path)
    return changed


def process_info(pid):
    directory = PROC_ROOT / str(pid)
    try:
        fields = (directory / "stat").read_text().rsplit(")", 1)[1].split()
        membership = (directory / "cgroup").read_text().strip()
        if not membership.startswith("0::/") or "\n" in membership:
            raise ValueError("Expected one unified cgroup membership")
        executable = (directory / "cmdline").read_bytes().split(b"\0", 1)[0]
        threads = []
        children = set()
        for task in (directory / "task").iterdir():
            try:
                tid = int(task.name)
                threads.append({"tid": tid, "policy": os.sched_getscheduler(tid),
                                "nice": os.getpriority(os.PRIO_PROCESS, tid)})
                children.update(map(int, (task / "children").read_text().split()))
            except ProcessLookupError:
                continue
            except FileNotFoundError:
                continue
        return {"pid": pid, "uid": directory.stat().st_uid, "state": fields[0],
                "parent": int(fields[1]), "start": int(fields[19]),
                "rss_bytes": int(fields[21]) * os.sysconf("SC_PAGE_SIZE"),
                "command": os.fsdecode(executable).rsplit("/", 1)[-1],
                "cgroup": membership[3:], "threads": threads, "children": sorted(children)}
    except (FileNotFoundError, ProcessLookupError):
        return None


def violations(config, rows, parent, quota, ancestors, available):
    reasons = []
    target = next((row for row in rows if row["pid"] == config["pid"]), None)
    if not target or target["start"] != config["start"] or target["parent"] != config["parent_pid"]:
        reasons.append("build identity changed")
    if not parent or parent["start"] != config["parent_start"] or parent["state"] == "Z":
        reasons.append("original build owner unavailable")
    allowance = cpu_percent(quota)
    if allowance is None or allowance > config["max_cpu_percent"]:
        reasons.append("build scope CPU limit exceeded")
    for path, limit in config["ancestor_limits"].items():
        allowed = cpu_percent(ancestors[path]) if path in ancestors else None
        if allowed is None or allowed > limit:
            reasons.append("shared host CPU limit changed: " + path)
    if any(row["cgroup"] != config["scope"] or row["uid"] != config["uid"] for row in rows):
        reasons.append("build process escaped scope or ownership")
    if any(not row["threads"] or any(thread["policy"] != os.SCHED_IDLE or thread["nice"] != 19
                                     for thread in row["threads"]) for row in rows if row["state"] != "Z"):
        reasons.append("build thread priority changed")
    if sum(row["rss_bytes"] for row in rows) >= config["max_rss_bytes"] or available <= config["min_available_bytes"]:
        reasons.append("memory safety margin reached")
    if sum(bool(COMPILER.fullmatch(row["command"])) for row in rows if row["state"] != "Z") > config["max_compilers"]:
        reasons.append("compiler concurrency exceeded")
    return reasons


def stop_identity(row, uid):
    descriptor = None
    try:
        descriptor = os.pidfd_open(row["pid"])
        current = process_info(row["pid"])
        if not current or current["start"] != row["start"] or current["uid"] != uid:
            return False
        signal.pidfd_send_signal(descriptor, signal.SIGSTOP)
        return True
    except ProcessLookupError:
        return False
    finally:
        if descriptor is not None:
            os.close(descriptor)


def freeze_build(scope, rows, config):
    outcome = {"frozen": False, "stopped": [], "errors": []}
    try:
        (scope / "cgroup.freeze").write_text("1")
        outcome["frozen"] = True
    except OSError as error:
        outcome["errors"].append(str(error))
    ordered = sorted(rows, key=lambda row: row["pid"] != config["pid"])
    for row in ordered:
        try:
            if stop_identity(row, config["uid"]):
                outcome["stopped"].append(row["pid"])
        except OSError as error:
            outcome["errors"].append(str(error))
    return outcome


def collect_rows(config, scope, previous):
    pending = {config["pid"]}
    try:
        pending.update(map(int, (scope / "cgroup.procs").read_text().split()))
    except FileNotFoundError:
        pass
    pending.update(row["pid"] for row in previous)
    known = {row["pid"]: row["start"] for row in previous}
    known[config["pid"]] = config["start"]
    rows = {}
    visited = set()
    while pending:
        pid = pending.pop()
        if pid in visited:
            continue
        visited.add(pid)
        row = process_info(pid)
        if row is None or (pid in known and row["start"] != known[pid]):
            continue
        rows[pid] = row
        pending.update(row["children"])
    return list(rows.values())


def save(prefix, suffix, value):
    destination = Path(str(prefix) + suffix)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(destination)


def progress(path):
    with Path(path).open("rb") as source:
        source.seek(max(0, os.fstat(source.fileno()).st_size - 32768))
        matches = re.findall(rb"\[(\d+)/(\d+)\]", source.read())
    if not matches:
        return None
    completed, total = map(int, matches[-1])
    return {"completed": completed, "total": total, "remaining": total - completed}


def interrupted(number, frame):
    del frame
    raise RuntimeError("Guard interrupted by signal " + str(number))


def monitor(config, prefix):
    if not hasattr(os, "pidfd_open") or not hasattr(signal, "pidfd_send_signal"):
        raise RuntimeError("Use a Linux Python with pidfd support, such as /usr/bin/python3")
    if config["uid"] != os.getuid() or not config["input_hashes"]:
        raise ValueError("Build owner and input hashes must be explicit")
    relative = Path(config["scope"])
    if not relative.is_absolute() or ".." in relative.parts or not relative.name.endswith(".scope"):
        raise ValueError("Expected an absolute scope path")
    scope = CGROUP_ROOT / str(relative).lstrip("/")
    rows = []
    previous_cpu = None
    previous_time = None
    next_hash = 0
    samples = 0
    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(number, interrupted)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    try:
        with Path(str(prefix) + "-samples.jsonl").open("x") as output:
            while True:
                started = time.monotonic()
                target = process_info(config["pid"])
                if not target or target["start"] != config["start"] or target["state"] == "Z":
                    remaining = collect_rows(config, scope, rows)
                    live = [row for row in remaining if row["state"] != "Z"]
                    if live:
                        rows = live
                        raise RuntimeError("Build owner exited with surviving workers")
                    save(prefix, "-summary.json", {"state": "exited", "samples": samples,
                         "exit_code": None, "exit_status_owner": config["parent_pid"],
                         "changed_inputs": changed_inputs(config["input_hashes"]), "timestamp": time.time()})
                    return 0
                rows = collect_rows(config, scope, rows)
                parent = process_info(config["parent_pid"])
                quota = (scope / "cpu.max").read_text().strip()
                ancestors = {}
                for directory in scope.parents:
                    if directory == CGROUP_ROOT:
                        break
                    ancestors["/" + str(directory.relative_to(CGROUP_ROOT))] = (directory / "cpu.max").read_text().strip()
                available = int(re.search(r"^MemAvailable:\s+(\d+)", (PROC_ROOT / "meminfo").read_text(), re.M)[1]) * 1024
                reasons = violations(config, rows, parent, quota, ancestors, available)
                if time.monotonic() >= next_hash:
                    changed = changed_inputs(config["input_hashes"])
                    if changed:
                        reasons.append("build inputs changed: " + ", ".join(changed))
                    next_hash = time.monotonic() + 60
                if reasons:
                    raise RuntimeError("; ".join(reasons))
                before_read = time.monotonic()
                usage = int(dict(line.split() for line in (scope / "cpu.stat").read_text().splitlines())["usage_usec"])
                after_read = time.monotonic()
                timestamp = (before_read + after_read) / 2
                cpu = None if previous_cpu is None else (usage - previous_cpu) / (timestamp - previous_time) / 10000
                limits = [cpu_percent(value) for value in [quota, *ancestors.values()]]
                sample = {"timestamp": time.time(), "monotonic": timestamp, "cpu_read_seconds": after_read - before_read,
                          "scope_quota": quota, "ancestor_quotas": ancestors,
                          "effective_cpu_percent": min(value for value in limits if value is not None),
                          "usage_usec": usage, "interval_cpu_percent": cpu,
                          "rss_bytes": sum(row["rss_bytes"] for row in rows), "available_bytes": available,
                          "processes": rows, "progress": progress(config["log"]), "guard": None}
                output.write(json.dumps(sample) + "\n")
                output.flush()
                save(prefix, "-latest.json", sample)
                if samples == 0:
                    save(prefix, "-ready.json", {"pid": os.getpid(), "timestamp": time.time(), "config": config})
                samples += 1
                previous_cpu, previous_time = usage, timestamp
                time.sleep(max(0.05, 1 - (time.monotonic() - started)))
    except Exception as error:
        try:
            rows = collect_rows(config, scope, rows)
        except (OSError, ValueError):
            pass
        outcome = freeze_build(scope, rows, config)
        save(prefix, "-ALERT.json", {"reason": str(error), "outcome": outcome, "timestamp": time.time()})
        return 2


def main():
    parser = argparse.ArgumentParser(description="Guard an existing build; never launch, resume or restart it.")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    return monitor(json.loads(arguments.config.read_text()), arguments.output)


if __name__ == "__main__":
    raise SystemExit(main())

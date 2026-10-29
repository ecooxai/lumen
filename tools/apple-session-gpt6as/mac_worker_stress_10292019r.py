#!/usr/bin/env python3
"""Bounded test-only Mac worker soak, bound to a verified native product build."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

TEST_SOURCE = '/* Repeated real Lumen worker lifetime validation, using the frozen native\n * lifecycle fixture unchanged. This test does not constitute GUI scrolling,\n * iOS device, or IPA acceptance. */\n#define main worker_async_single_run_main\n#include "test_worker_async_lifecycle.cc"\n#undef main\n#include <dirent.h>\n#include <errno.h>\n#include <fcntl.h>\n#include <fstream>\n#ifdef __APPLE__\n#include <libproc.h>\n#endif\n\nstatic int journal_fd = -1;\nstatic unsigned journal_cycle = 0;\nstatic void journal(const char *phase, const char *name) {\n    if (journal_fd < 0) return;\n    const int saved_errno = errno;\n    const int stdout_fd = fcntl(STDOUT_FILENO, F_GETFD);\n    const int stderr_fd = fcntl(STDERR_FILENO, F_GETFD);\n    char line[768];\n    const int length = snprintf(line, sizeof line,\n        "{\\"cycle\\":%u,\\"phase\\":\\"%s\\",\\"case\\":\\"%s\\",\\"checks\\":%d,\\"failures\\":%d,\\"stdout_error\\":%d,\\"stderr_error\\":%d,\\"stdout_fd\\":%d,\\"stderr_fd\\":%d,\\"pid\\":%d}\\n",\n        journal_cycle, phase, name, checks, failures, ferror(stdout), ferror(stderr), stdout_fd, stderr_fd, getpid());\n    if (length > 0 && static_cast<size_t>(length) < sizeof line) (void)write(journal_fd, line, static_cast<size_t>(length));\n    errno = saved_errno;\n}\nstatic void journal_at_exit() {\n    journal("atexit", "process");\n    if (journal_fd >= 0) close(journal_fd);\n}\nstatic void run_case(const char *name, void (*function)()) {\n    journal("before", name);\n    function();\n    journal("after", name);\n}\n\nstruct ResourceCheckpoint {\n    long rss_kib = -1, threads = -1, fds = -1;\n};\n\nstatic ResourceCheckpoint resource_checkpoint(unsigned cycle, bool emit = true) {\n    ResourceCheckpoint resource;\n#ifdef __APPLE__\n    proc_taskinfo task{};\n    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof task) == sizeof task) {\n        resource.rss_kib = static_cast<long>(task.pti_resident_size / 1024);\n        resource.threads = task.pti_threadnum;\n    }\n    const int capacity = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);\n    if (capacity >= 0) {\n        std::vector<proc_fdinfo> descriptors(static_cast<size_t>(capacity) / sizeof(proc_fdinfo) + 8);\n        const int size = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, descriptors.data(),\n                                     static_cast<int>(descriptors.size() * sizeof(proc_fdinfo)));\n        if (size >= 0) resource.fds = size / sizeof(proc_fdinfo);\n    }\n#else\n    std::ifstream status("/proc/self/status");\n    std::string line;\n    while (std::getline(status, line)) {\n        if (line.rfind("VmRSS:", 0) == 0) resource.rss_kib = strtol(line.c_str() + 6, nullptr, 10);\n        if (line.rfind("Threads:", 0) == 0) resource.threads = strtol(line.c_str() + 8, nullptr, 10);\n    }\n    DIR *directory = opendir("/proc/self/fd");\n    if (directory) {\n        resource.fds = -1; // Exclude this directory\'s own temporary descriptor.\n        while (dirent *entry = readdir(directory)) if (entry->d_name[0] != \'.\') ++resource.fds;\n        closedir(directory);\n    }\n#endif\n    if (emit) {\n        printf("RESOURCE_CHECKPOINT {\\"cycle\\":%u,\\"rss_kib\\":%ld,\\"threads\\":%ld,\\"fds\\":%ld,\\"pending\\":%d,\\"workers\\":%zu}\\n",\n               cycle, resource.rss_kib, resource.threads, resource.fds, net_pending(), g_workers.size());\n        fflush(stdout);\n    }\n    return resource;\n}\n\nint main(int argc, char **argv) {\n    unsigned cycles = argc > 1 ? static_cast<unsigned>(strtoul(argv[1], nullptr, 10)) : 20;\n    if (cycles < 5 || cycles > 100) { fprintf(stderr, "cycles must be from 5 to 100\\n"); return 2; }\n    if (argc > 2) {\n        journal_fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0600);\n        if (journal_fd < 0) { perror("stress journal"); return 2; }\n        if (std::atexit(journal_at_exit)) return 2;\n    }\n    dom_init(); net_init(4); font_init(); js_global_init(argv[0]);\n    ResourceCheckpoint warm;\n    for (unsigned cycle = 1; cycle <= cycles; ++cycle) {\n        journal_cycle = cycle;\n        printf("STRESS_CYCLE %u of %u\\n", cycle, cycles); fflush(stdout);\n        run_case("concurrency", check_concurrency);\n        run_case("fetch_abort", check_fetch_abort);\n        run_case("completion_burst", check_fetch_completion_burst);\n        run_case("falsy_abort_reasons", check_falsy_abort_reasons);\n        run_case("owner_loop_independence", check_owner_loop_independence);\n        run_case("xhr_reopen", check_xhr_reopen);\n        run_case("xhr_timeout_and_sync", check_xhr_timeout_and_sync);\n        run_case("xhr_reentrant_open", check_xhr_reentrant_open);\n        run_case("terminate", [] { check_exit_cancellation(false); });\n        run_case("self_close", [] { check_exit_cancellation(true); });\n        CHECK("cycle leaves no registered workers", g_workers.empty());\n        CHECK("cycle leaves no pending network ownership", net_pending() == 0);\n        ResourceCheckpoint current = resource_checkpoint(cycle, false);\n        if (cycle > 4 && (current.threads > warm.threads || current.fds > warm.fds)) {\n            // A detached worker releases its last Lumen ownership and signals\n            // completion before pthread return/TLS teardown is visible to the\n            // kernel. Preserve that initial observation, then bound settling.\n            ResourceCheckpoint initial = current;\n            const auto begun = std::chrono::steady_clock::now();\n            const auto deadline = begun + std::chrono::milliseconds(500);\n            while ((current.threads > warm.threads || current.fds > warm.fds)\n                   && std::chrono::steady_clock::now() < deadline) {\n                std::this_thread::sleep_for(std::chrono::milliseconds(5));\n                current = resource_checkpoint(cycle, false);\n            }\n            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(\n                std::chrono::steady_clock::now() - begun).count();\n            printf("RESOURCE_SETTLE {\\"cycle\\":%u,\\"initial_threads\\":%ld,\\"initial_fds\\":%ld,\\"final_threads\\":%ld,\\"final_fds\\":%ld,\\"elapsed_ms\\":%lld,\\"limit_ms\\":500}\\n",\n                   cycle, initial.threads, initial.fds, current.threads, current.fds,\n                   static_cast<long long>(elapsed));\n        }\n        current = resource_checkpoint(cycle);\n        CHECK("native resource checkpoint is readable", current.rss_kib > 0 && current.threads > 0 && current.fds >= 0);\n        if (cycle == 4) warm = current;\n        if (cycle > 4) {\n            CHECK("settled file descriptor count does not accumulate", current.fds <= warm.fds);\n            CHECK("settled native thread count does not accumulate", current.threads <= warm.threads);\n        }\n        if (failures) break;\n    }\n    CHECK("stress workers complete before process shutdown", js_workers_shutdown(3000));\n    CHECK("stress requests finalize before process shutdown", drain_network());\n    journal("idle_begin", "process");\n    printf("STRESS_IDLE_BEGIN\\n"); fflush(stdout);\n    std::this_thread::sleep_for(std::chrono::seconds(12));\n    resource_checkpoint(cycles + 1);\n    net_shutdown();\n    printf("RESULTS worker-lifetime-stress %s (%d/%d checks)\\n", failures ? "FAIL" : "PASS", checks - failures, checks);\n    fflush(stdout);\n    journal("return", "process");\n    return failures ? 1 : 0;\n}\n'
FROZEN_ASYNC_SHA = "f2d2b205e23ae691251c2d63869f0f1137666d14ea1323aa85eab7ef5a63ae83"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-summary", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if str(root) != "/Users/devin/repos/lumen" or sys.platform != "darwin":
        raise SystemExit("This carrier requires the real canonical Mac repository")
    build_summary = root / args.build_summary
    product = json.loads(build_summary.read_text())
    if not product.get("passed"):
        raise SystemExit("Product build summary does not pass")
    manifest = root / product["compiledSourceManifest"]
    if sha(manifest) != product["sourceManifestSHA256"]:
        raise SystemExit("Compiled source manifest differs")
    sources = json.loads(manifest.read_text())
    for name, digest in sources.items():
        if sha(root / name) != digest:
            raise SystemExit("Current source differs from product build: " + name)
    if sha(root / "tests/test_worker_async_lifecycle.cc") != FROZEN_ASYNC_SHA:
        raise SystemExit("Frozen asynchronous lifecycle fixture differs")
    product_artifacts = product["artifacts"]
    for name, row in product_artifacts.items():
        if sha(root / row["path"]) != row["sha256"]:
            raise SystemExit("Immutable product artifact differs: " + name)
    out = root / args.output
    out.mkdir(parents=True, exist_ok=False)
    began = time.monotonic()
    report = {
        "startedUTC": datetime.now(timezone.utc).isoformat(),
        "platform": "Darwin arm64 native C/V8 test executable",
        "sourceBaseCommit": product["sourceBaseCommit"],
        "compiledSourceManifestSHA256": sha(manifest),
        "productBuildSummary": str(build_summary.relative_to(root)),
        "productBuildSummarySHA256": sha(build_summary),
        "productArtifacts": product_artifacts,
        "cyclesRequested": 100,
        "scope": "Actual native worker ownership soak; not GUI scrolling or an iOS/IPA run",
        "stages": [], "passed": False,
    }

    def save():
        (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")

    def measured(name, command, limit):
        prefix = out / name
        result = subprocess.run([
            sys.executable, "tools/monitor_process.py", "--duration", str(limit),
            "--settle", "0", "--output", str(prefix.relative_to(root)), "--", *command,
        ], capture_output=True, text=True, timeout=limit + 35)
        (out / (name + "-collector.log")).write_text(result.stdout + result.stderr)
        data = json.loads(prefix.with_suffix(".json").read_text())
        ok = (data.get("natural_exit_code") == 0 and not data.get("collector_error")
              and not data.get("ended_by_monitor")
              and not data.get("owned_group_cleanup", {}).get("error"))
        report["stages"].append({
            "name": name, "passed": ok, "command": command,
            "durationSeconds": data.get("duration_s"),
            "monitor": str(prefix.with_suffix(".json").relative_to(root)),
            "monitorSHA256": sha(prefix.with_suffix(".json")),
            "logSHA256": sha(prefix.with_suffix(".log")),
            "cpuUnit": data.get("cpu_unit"), "rssUnit": "MiB",
            "cpuP95Percent": data.get("steady_interval_cpu_p95_percent"),
            "cpuPeakPercent": data.get("interval_cpu_peak_percent"),
            "rssPeakMiB": data.get("rss_peak_mb"),
            "maxSamplingGapSeconds": data.get("max_sampling_gap_s"),
        })
        save()
        if not ok:
            raise RuntimeError(name + " failed; original log and monitor preserved")
        return data

    try:
        test_path = root / "tests/test_worker_lifetime_stress.cc"
        if test_path.exists() and test_path.read_text() != TEST_SOURCE:
            raise RuntimeError("Existing stress fixture differs; preserve it")
        if not test_path.exists():
            test_path.write_text(TEST_SOURCE)
        report["stressFixtureSHA256"] = sha(test_path)
        report["frozenAsyncFixtureSHA256"] = FROZEN_ASYNC_SHA
        build = product["buildCache"]
        if not re.fullmatch(r"[A-Za-z0-9_./-]+", build):
            raise RuntimeError("Unexpected build cache path")
        fragment = out / "stress.mk"
        fragment.write_text(
            "$(BUILD)/tests/test_worker_lifetime_stress.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG -I$(BUILD)/src/js\n"
            "$(BUILD)/tests/test_worker_lifetime_stress.cc.o: $(BUILD)/tests/test_worker_async_lifecycle.cc.o\n"
            "$(BUILD)/test_worker_lifetime_stress: $(call OBJ,tests/test_worker_lifetime_stress.cc $(CORE_SRC) $(filter-out src/js/worker.cc,$(JS_SRC))) $(V8LINK_RSP)\n"
            "\t$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS) -lproc\n"
        )
        measured("build", ["make", "-j1", "-f", "Makefile", "-f", str(fragment),
                  "BUILD=" + build, "MODE=release", "RTC=1", "WT=1",
                  build + "/test_worker_lifetime_stress"], 180)
        binary = root / build / "test_worker_lifetime_stress"
        if not binary.is_file() or binary.stat().st_size < 1024 or not os.access(binary, os.X_OK):
            raise RuntimeError("Stress linker did not produce a nonempty executable")
        digest = sha(binary)
        frozen_binary = out / ("worker-stress-" + digest[:16])
        shutil.copy2(binary, frozen_binary)
        frozen_binary.chmod(0o555)
        report["stressBinary"] = {"path": str(frozen_binary.relative_to(root)), "sha256": digest}
        measured("run", [str(frozen_binary), "100", str(out / "journal.jsonl")], 240)
        log = (out / "run.log").read_text(errors="replace")
        checkpoints = [json.loads(line.split(" ", 1)[1]) for line in log.splitlines()
                       if line.startswith("RESOURCE_CHECKPOINT ")]
        resource_settling = [json.loads(line.split(" ", 1)[1]) for line in log.splitlines()
                            if line.startswith("RESOURCE_SETTLE ")]
        journal = [json.loads(line) for line in (out / "journal.jsonl").read_text().splitlines()]
        matches = re.findall(r"RESULTS worker-lifetime-stress PASS \((\d+)/(\d+) checks\)", log)
        returns = [row for row in journal if row["phase"] == "return"]
        exits = [row for row in journal if row["phase"] == "atexit"]
        if (not matches or matches[-1][0] != matches[-1][1]
                or len(checkpoints) != 101 or checkpoints[-1]["cycle"] != 101
                or len(returns) != 1 or len(exits) != 1
                or any(row["failures"] or row["stdout_error"] or row["stderr_error"]
                       or row["stdout_fd"] < 0 or row["stderr_fd"] < 0 for row in journal)
                or any(row["pending"] or row["workers"] for row in checkpoints)):
            raise RuntimeError("Native result, resource checkpoints, or independent journal incomplete")
        warm = next(row for row in checkpoints if row["cycle"] == 4)
        report["checksPassed"] = int(matches[-1][0])
        report["checksTotal"] = int(matches[-1][1])
        report["cyclesCompleted"] = 100
        report["journalRecords"] = len(journal)
        report["resources"] = {
            "warmCycle": 4, "warmRSSMiB": warm["rss_kib"] / 1024,
            "finalRSSMiB": checkpoints[-1]["rss_kib"] / 1024,
            "warmToFinalRSSMiB": (checkpoints[-1]["rss_kib"] - warm["rss_kib"]) / 1024,
            "checkpointRSSPeakMiB": max(row["rss_kib"] for row in checkpoints) / 1024,
            "warmThreads": warm["threads"], "finalThreads": checkpoints[-1]["threads"],
            "warmFDs": warm["fds"], "finalFDs": checkpoints[-1]["fds"],
            "journalOwnsOneFD": True, "finalPending": checkpoints[-1]["pending"],
            "finalWorkers": checkpoints[-1]["workers"], "checkpoints": checkpoints,
            "boundedOSThreadSettlingMilliseconds": 500, "settlingObservations": resource_settling,
        }
        for name, digest in sources.items():
            if sha(root / name) != digest:
                raise RuntimeError("Compiled source changed during soak: " + name)
        for name, row in product_artifacts.items():
            if sha(root / row["path"]) != row["sha256"]:
                raise RuntimeError("Product artifact changed during soak: " + name)
        report["passed"] = True
    except Exception as error:
        report["error"] = type(error).__name__ + ": " + str(error)
    finally:
        report["finishedUTC"] = datetime.now(timezone.utc).isoformat()
        report["durationSeconds"] = round(time.monotonic() - began, 3)
        save()
        print(json.dumps({"passed": report["passed"], "cycles": report.get("cyclesCompleted"),
                          "checks": report.get("checksPassed"), "error": report.get("error"),
                          "durationSeconds": report["durationSeconds"],
                          "summary": str((out / "summary.json").relative_to(root))}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

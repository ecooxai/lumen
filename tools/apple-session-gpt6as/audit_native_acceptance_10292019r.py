#!/usr/bin/env python3
"""Read-only recomputation of native GUI/stress evidence, plus a raw archive."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import csv
import hashlib
import importlib.util
import json
import math
import re
import statistics
import tarfile
import time

RUNNER_SHA = "b957f810f6f49a9133b57165af9e369e12b7514da4fab3c8d8d921bb01f08f1a"
TELEMETRY_SHA = "70a9c367e6b7649e5469167c4431f7dc0cb4ad428a9d89ceb291e7834aaa4ec2"
STRESS_FIXTURE_SHA = "e066464a0952675624cf29249f6398c8c877863b353f1dc12c18063d4dc1715d"
MIB = 1048576


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def p95(values):
    return sorted(values)[max(0, math.ceil(len(values) * .95) - 1)]


def finite(value):
    return isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-summary", required=True)
    parser.add_argument("--resources", required=True)
    parser.add_argument("--stress", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if str(root) != "/Users/devin/repos/lumen":
        raise SystemExit("Expected the real canonical Mac repository")
    paths = [(root / value).resolve() for value in (args.build_summary, args.resources, args.stress, args.output)]
    for path in paths:
        path.relative_to(root / "out")
    build_path, resource_dir, stress_dir, out = paths
    out.mkdir(parents=True, exist_ok=False)
    began = time.monotonic()
    report = {"startedUTC": datetime.now(timezone.utc).isoformat(), "auditorVersion": 2, "issues": [], "phases": {},
              "scope": "Exact native Mac binary, synthetic SDL scroll,20 actual navigations and100-cycle native worker fixture; not iPad/IPA or universal resource guarantee",
              "cpuConvention": "Cumulative process CPU seconds / elapsed seconds *100;100% is one logical core",
              "passed": False}
    issues = report["issues"]

    def require(condition, description):
        if not condition:
            issues.append(description)

    try:
        build = json.loads(build_path.read_text())
        resource = json.loads((resource_dir / "summary.json").read_text())
        stress = json.loads((stress_dir / "summary.json").read_text())
        artifact = build["artifacts"]["lumen"]
        require(build.get("passed") and sha(root / artifact["path"]) == artifact["sha256"], "Native build or immutable product hash does not pass")
        require(resource.get("accepted") and resource.get("artifact") == artifact and
                resource.get("buildSummarySHA256") == sha(build_path), "GUI resource summary is not bound to this accepted build")
        require(stress.get("passed") and stress.get("productBuildSummarySHA256") == sha(build_path) and
                stress.get("productArtifacts") == build["artifacts"], "Native stress summary is not bound to this accepted build")
        manifest = root / build["compiledSourceManifest"]
        require(sha(manifest) == build["sourceManifestSHA256"], "Compiled source manifest hash differs")
        report.update(buildSummarySHA256=sha(build_path), sourceBaseCommit=build["sourceBaseCommit"],
                      sourceManifestSHA256=sha(manifest), productArtifact=artifact)
        runner_path = root / "tools/apple-session-gpt6as/remote_native_lifecycle.py"
        require(sha(runner_path) == RUNNER_SHA, "Reviewed workload source differs")
        if sha(runner_path) != RUNNER_SHA:
            raise RuntimeError("Refuse to import a changed workload source")
        spec = importlib.util.spec_from_file_location("fixed_native_fixture", runner_path)
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        from urllib.parse import parse_qs, urlsplit

        for scenario in ("scroll", "navigation"):
            directory = resource_dir / scenario
            receipt = json.loads((directory / "receipt.json").read_text())
            raw = [json.loads(line) for line in (directory / "process-samples.jsonl").read_text().splitlines() if line.strip()]
            require(receipt.get("accepted") and not receipt.get("issues"), scenario + ": native resource receipt failed")
            require(receipt.get("binary_sha256") == artifact["sha256"] == receipt.get("binary_sha256_after"), scenario + ": artifact binding differs")
            require(receipt.get("telemetry_sha256") == TELEMETRY_SHA, scenario + ": telemetry source differs")
            require(receipt.get("samples_sha256") == sha(directory / "process-samples.jsonl"), scenario + ": raw sample hash differs")
            require(receipt.get("natural_app_return_code") == 0 and all(
                (receipt.get(key) or {}).get("return_code") == 0 and (receipt.get(key) or {}).get("forced") is False
                for key in ("app_termination", "sampler_termination")), scenario + ": owned process cleanup is not clean")
            require(not any(row.get("type") == "error" for row in raw), scenario + ": collector error row exists")
            samples = [row for row in raw if row.get("type") == "sample"]
            previous, crosschecks, disagreements, roots, starts = None, 0, [], set(), set()
            for row in samples:
                group = row.get("groups", {}).get("lumen", {})
                if group.get("observed"):
                    roots.update(group.get("roots", []))
                    starts.update(p.get("start_time") for p in group.get("processes", []) if p.get("pid") == receipt.get("app_pid"))
                if previous is not None and group.get("observed") and not group.get("phase_boundary"):
                    old = {(p.get("pid"), p.get("start_time")): p for p in previous.get("groups", {}).get("lumen", {}).get("processes", [])}
                    current = group.get("processes", [])
                    interval = row.get("interval_seconds")
                    if finite(interval) and interval > 0 and current and all((p.get("pid"), p.get("start_time")) in old for p in current):
                        delta = sum(p["cpu_seconds"] - old[(p.get("pid"), p.get("start_time"))]["cpu_seconds"] for p in current)
                        calculated = delta / interval *100
                        crosschecks += 1
                        if delta < 0 or not finite(group.get("cpu_percent")) or abs(calculated - group["cpu_percent"]) > .002:
                            disagreements.append({"utc": row.get("utc"), "calculated": calculated, "recorded": group.get("cpu_percent")})
                previous = row
            require(crosschecks >= 10 and not disagreements, scenario + ": raw cumulative CPU crosscheck failed")
            require(roots == {receipt.get("app_pid")} and len(starts) == 1 and None not in starts, scenario + ": process identity changed")
            result = {"cumulativeCPUIntervalsCrosschecked": crosschecks, "CPUDisagreements": disagreements, "phases": {}}
            wanted = {"scrolling": (30,30), "idle:after": (45,3)} if scenario == "scroll" else {"idle:before": (10,3), "navigation": (30,None), "idle:after": (45,3)}
            phase_rows = {}
            for phase, (duration, ceiling) in wanted.items():
                rows = [r for r in samples if r.get("phase") == phase]
                phase_rows[phase] = rows
                require(rows and all(r.get("groups", {}).get("lumen", {}).get("observed") for r in rows), scenario + ": missing " + phase + " process observations")
                cpu = [r["groups"]["lumen"].get("cpu_percent") for r in rows if not r["groups"]["lumen"].get("phase_boundary")]
                rss = [r["groups"]["lumen"]["rss_bytes"] / MIB for r in rows]
                times = [datetime.fromisoformat(r["utc"]) for r in rows]
                gaps = [(b-a).total_seconds() for a,b in zip(times, times[1:])]
                span = (times[-1]-times[0]).total_seconds() if len(times)>1 else 0
                monotonic_span = rows[-1]["elapsed_seconds"] - rows[0]["elapsed_seconds"] if rows else 0
                require(len(rows) >= max(10,duration) and len(cpu) >=10 and min(span,monotonic_span) >= duration, scenario + ": insufficient " + phase + " window")
                require(gaps and min(gaps)>0 and max(gaps)<=1.5 and all(finite(v) and v>=0 for v in cpu), scenario + ": invalid " + phase + " intervals")
                summary = {"samples": len(rows), "CPUIntervals": len(cpu), "durationSeconds": round(span,3),
                           "maximumGapSeconds": round(max(gaps),3), "CPUMeanPercent": round(statistics.mean(cpu),3),
                           "CPUMedianPercent": round(statistics.median(cpu),3), "CPU_P95Percent": round(p95(cpu),3),
                           "CPUPeakPercent": max(cpu), "RSSStartMiB": rss[0], "RSSEndMiB": rss[-1], "RSSPeakMiB": max(rss)}
                require(ceiling is None or p95(cpu)<=ceiling, scenario + ": CPU ceiling exceeded in " + phase)
                stated = receipt["phase_metrics"][phase]
                require(stated["samples"] == len(rows) and stated["observed_samples"] == len(rows) and stated["cpu_p95_percent"] == summary["CPU_P95Percent"], scenario + ": receipt disagrees with raw " + phase)
                result["phases"][phase] = summary
            reports = [r["value"] for r in receipt["reports"]]
            selected = [r for r in reports if r.get("kind") == ("ready" if scenario == "scroll" else "loaded")]
            expected = [0] if scenario == "scroll" else list(range(21))
            require([r.get("step") for r in selected] == expected, scenario + ": page sequence incomplete")
            for row in selected:
                nonce = parse_qs(urlsplit(row["url"]).query).get("nonce", [])
                require(len(nonce) == 1 and hashlib.sha256(fixture.fixture(scenario,row["step"],nonce[0])).hexdigest() == receipt["fixture_sha256_by_step"][str(row["step"])], scenario + ": served fixture hash differs")
            if scenario == "scroll":
                completed = [r for r in reports if r.get("kind") == "scroll-complete"]
                require(len(completed)==1 and completed[0].get("events",0)>=10 and completed[0].get("maxY",0)>=500, "Actual scroll movement not proved")
                capture = json.loads((directory / "native-window/receipt.json").read_text())
                require(capture.get("captured") and sha(root / capture["screenshot"]) == capture["screenshotSHA256"], "Owned native window capture hash differs")
                result["scrollMovement"] = completed
                result["ownedWindowCapture"] = {k:capture[k] for k in ("screenshot","screenshotSHA256","width","height")}
            else:
                require(all(r.get("checks")==80 and r.get("documents")==16 for r in selected), "Navigation page clone/attribute assertions incomplete")
                before = [r["groups"]["lumen"]["rss_bytes"]/MIB for r in phase_rows["idle:before"]][-5:]
                after = [r["groups"]["lumen"]["rss_bytes"]/MIB for r in phase_rows["idle:after"]][-15:]
                initial, final = statistics.median(before), statistics.median(after)
                memory = {"initial_median_mib":initial,"final_median_mib":final,"growth_mib":final-initial,"growth_fraction":(final-initial)/initial}
                require(len(before)==5 and len(after)==15 and memory == receipt["settled_rss_comparison"] and final-initial<=32 and (final-initial)<=initial*.2, "Settled RSS windows fail or differ")
                result["settledRSS"] = memory
                result["nativeCounters"] = receipt["native_counter_comparison"]
                native = result["nativeCounters"]
                require(native["final"]["workers"]==0 and all(native["final"][k]-native["initial"][k]<=50 for k in ("nodes","styles")), "Native page ownership counters grow")
                log_counters = [{"kind":line.split(":",1)[0],"values":{key:float(value) for key,value in re.findall(r"(\w+)=([\d.]+)",line)}}
                                for line in (directory/"lumen.log").read_text(errors="replace").splitlines() if line.startswith("lumen-mem")]
                recorded_counters = [{"kind":row["kind"],"values":row["values"]} for row in receipt["native_memory_counters"]]
                require(log_counters == recorded_counters, "Recorded native counters disagree with raw application output")
            report["phases"][scenario] = result

        journal = [json.loads(line) for line in (stress_dir/"journal.jsonl").read_text().splitlines()]
        checkpoints = stress["resources"]["checkpoints"]
        require(stress.get("stressFixtureSHA256") == STRESS_FIXTURE_SHA == sha(root/"tests/test_worker_lifetime_stress.cc"), "Stress fixture hash differs")
        require(stress.get("cyclesCompleted")==100 and stress.get("checksPassed")==stress.get("checksTotal")==12094, "Stress checks/cycles incomplete")
        stress_log = (stress_dir/"run.log").read_text(errors="replace")
        raw_checkpoints = [json.loads(line.split(" ",1)[1]) for line in stress_log.splitlines() if line.startswith("RESOURCE_CHECKPOINT ")]
        raw_cycles = [int(value) for value in re.findall(r"^STRESS_CYCLE (\d+) of 100$",stress_log,re.MULTILINE)]
        require(raw_checkpoints == checkpoints and raw_cycles == list(range(1,101)) and
                re.findall(r"^RESULTS worker-lifetime-stress PASS \((\d+)/(\d+) checks\)$",stress_log,re.MULTILINE)==[("12094","12094")],
                "Raw native stress checkpoints, cycles or result disagree with summary")
        require(len(journal)==2003 and len(checkpoints)==101 and [r["cycle"] for r in checkpoints]==list(range(1,102)), "Stress journal/checkpoint sequence incomplete")
        case_names = ["concurrency","fetch_abort","completion_burst","falsy_abort_reasons","owner_loop_independence","xhr_reopen","xhr_timeout_and_sync","xhr_reentrant_open","terminate","self_close"]
        expected_events = [(cycle,phase,name) for cycle in range(1,101) for name in case_names for phase in ("before","after")]
        observed_events = [(r["cycle"],r["phase"],r["case"]) for r in journal if r["phase"] in ("before","after")]
        require(observed_events == expected_events and [r["phase"] for r in journal[-3:]] == ["idle_begin","return","atexit"], "Stress journal return/exit or case order incomplete")
        require(journal[-1]["checks"] == journal[-2]["checks"] == 12094, "Stress return and atexit journal check counts differ")
        require(all(not r["failures"] and not r["stdout_error"] and not r["stderr_error"] and r["stdout_fd"]>=0 and r["stderr_fd"]>=0 for r in journal), "Stress journal records a failure or lost output")
        warm = checkpoints[3]
        require(all(r["pending"]==0 and r["workers"]==0 and (r["cycle"]<=4 or (r["threads"]<=warm["threads"] and r["fds"]<=warm["fds"])) for r in checkpoints), "Stress ownership, threads or descriptors accumulate")
        monitor = json.loads((stress_dir/"run.json").read_text())
        require(monitor.get("natural_exit_code")==0 and not monitor.get("ended_by_monitor") and not monitor.get("collector_error") and monitor["max_sampling_gap_s"]<=1.5, "Stress runtime monitor failed")
        with (stress_dir/"run.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        elapsed = [float(row["elapsed_s"]) for row in rows]
        csv_gaps = [b-a for a,b in zip(elapsed,elapsed[1:])]
        require(len(rows)>=90 and csv_gaps and min(csv_gaps)>0 and max(csv_gaps)<=1.5, "Stress raw CSV has missing or excessive sampling intervals")
        cpu = [float(row["interval_cpu_percent"]) for row in rows if row["interval_cpu_percent"]]
        rss = [float(row["rss_kb"])/1024 for row in rows]
        report["stress"] = {"cycles":100,"checks":12094,"journalRecords":len(journal),"checkpoints":len(checkpoints),
                            "durationSeconds":monitor["duration_s"],"CPU_P95Percent":round(p95(cpu),3),"CPUPeakPercent":max(cpu),
                            "RSSPeakMiB":max(rss),"warmRSSMiB":warm["rss_kib"]/1024,"finalRSSMiB":checkpoints[-1]["rss_kib"]/1024,
                            "warmThreads":warm["threads"],"finalThreads":checkpoints[-1]["threads"],
                            "warmFDs":warm["fds"],"finalFDs":checkpoints[-1]["fds"],"maximumGapSeconds":monitor["max_sampling_gap_s"],
                            "boundedSettlingObservations":stress["resources"].get("settlingObservations",[])}
        report["passed"] = not issues

        inputs = {build_path, manifest, runner_path, root/"tests/test_worker_lifetime_stress.cc", root/"tests/test_worker_async_lifecycle.cc"}
        for directory in (resource_dir,stress_dir):
            for path in directory.rglob("*"):
                relative = path.relative_to(directory)
                if path.is_file() and not path.is_symlink() and "profile" not in relative.parts and path.suffix in (".json",".jsonl",".csv",".log",".png",".txt"):
                    inputs.add(path)
        if sum(p.stat().st_size for p in inputs)>64*MIB:
            raise RuntimeError("Raw evidence exceeds bounded64MiB archive input budget")
        raw_manifest = {str(path.relative_to(root)):{"sha256":sha(path),"bytes":path.stat().st_size} for path in sorted(inputs)}
        (out/"raw-manifest.json").write_text(json.dumps(raw_manifest,indent=2)+"\n")
        archive = out/"GPT6-Astra-Pro_builtin-agents_devinmac3d-native-acceptance-raw.tar.gz"
        with tarfile.open(archive,"w:gz") as stream:
            for path in sorted(inputs):
                stream.add(path,arcname=str(path.relative_to(root)),recursive=False)
            stream.add(out/"raw-manifest.json",arcname="raw-manifest.json",recursive=False)
        require(all(sha(root/name)==row["sha256"] for name,row in raw_manifest.items()), "Raw files changed while archiving")
        report["archive"] = {"path":str(archive.relative_to(root)),"sha256":sha(archive),"bytes":archive.stat().st_size,"files":len(inputs)}
        report["passed"] = not issues
    except Exception as error:
        issues.append(type(error).__name__+": "+str(error))
        report["passed"] = False
    report["finishedUTC"] = datetime.now(timezone.utc).isoformat()
    report["durationSeconds"] = round(time.monotonic()-began,3)
    (out/"summary.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({"passed":report["passed"],"issues":issues,"summary":str((out/"summary.json").relative_to(root)),
                      "archive":report.get("archive"),"durationSeconds":report["durationSeconds"]}),flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

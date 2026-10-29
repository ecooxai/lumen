#!/usr/bin/env python3
"""Real synchronous HTTP wait versus Lumen's JavaScript watchdog budget.

This uses public XMLHttpRequest and an actual jsrun binary. It does not mock
the native clock, V8, networking or watchdog. Run under monitor_process.py.
The busy-loop controls intentionally consume approximately one CPU core.
"""
import argparse
import datetime
import hashlib
import http.server
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import time
import urllib.parse


CASES = [
    {"name": "native_wait_excluded", "budget": 500, "pre": 0, "post": 0, "loop": "none", "terminate": False},
    {"name": "busy_before_wait_terminates", "budget": 500, "pre": 0, "post": 0, "loop": "before", "terminate": True},
    {"name": "busy_after_wait_terminates", "budget": 500, "pre": 0, "post": 0, "loop": "after", "terminate": True},
    {"name": "prior_active_budget_preserved", "budget": 1000, "pre": 700, "post": 750, "loop": "none", "terminate": True},
    {"name": "combined_active_work_below_budget", "budget": 1000, "pre": 250, "post": 250, "loop": "none", "terminate": False},
]
DELAY_MS = 1400
MARKER = "SYNC_WATCHDOG_MARK "


def make_html(case):
    # Follow-up classic script and timer also expose a stale queued interrupt
    # crossing the terminated task boundary.
    config = json.dumps(case, separators=(",", ":"))
    return (r'''<!doctype html><meta charset="utf-8"><title>Sync XHR watchdog</title>
<script>
'use strict';
const config=__CONFIG__;
function mark(phase,extra){console.log('SYNC_WATCHDOG_MARK '+JSON.stringify({phase,...extra}));}
function busy(ms){const until=performance.now()+ms;while(performance.now()<until){}}
mark('started');
if(config.loop==='before'){while(true){}}
busy(config.pre);
mark('before_wait');
const xhr=new XMLHttpRequest();
xhr.open('GET','/slow?case='+encodeURIComponent(config.name),false);
const started=performance.now();
xhr.send();
mark('after_wait',{status:xhr.status,body:xhr.responseText,elapsed:performance.now()-started});
if(config.loop==='after'){while(true){}}
busy(config.post);
mark('completed');
</script>
<script>
console.log('SYNC_WATCHDOG_MARK '+JSON.stringify({phase:'next_script'}));
setTimeout(()=>{console.log('SYNC_WATCHDOG_MARK '+JSON.stringify({phase:'next_task'}));document.documentElement.setAttribute('data-sync-watchdog-done','1');},10);
</script>'''.replace("__CONFIG__", config)).encode()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path == "/":
            name = urllib.parse.parse_qs(parsed.query).get("case", [""])[0]
            case = next((item for item in CASES if item["name"] == name), None)
            if case is None:
                self.send_error(404)
                return
            body, content_type = make_html(case), "text/html; charset=utf-8"
        elif parsed.path == "/slow":
            name = urllib.parse.parse_qs(parsed.query).get("case", [""])[0]
            row = {"case": name, "startedMonotonic": time.monotonic(), "responseWritten": False}
            with self.server.records_lock:
                self.server.records.append(row)
            time.sleep(DELAY_MS / 1000)
            row["waitSeconds"] = time.monotonic() - row["startedMonotonic"]
            body, content_type = b"SYNC_WATCHDOG_OK", "text/plain; charset=utf-8"
        else:
            self.send_error(404)
            return
        try:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
            if parsed.path == "/slow":
                row["responseWritten"] = True
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file() or binary.stat().st_size < 1024 or not os.access(binary, os.X_OK):
        parser.error("--binary must be a nonempty native Lumen jsrun executable")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        parser.error("output already exists; preserve the prior run")
    began = time.monotonic()
    report = {"startedUTC": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "scope": "Focused native synchronous XHR watchdog regression, not upstream WPT",
              "binary": str(binary), "binarySHA256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "fixtureSHA256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "serverDelayMilliseconds": DELAY_MS, "cases": [], "passed": False}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    server.records = []
    server.records_lock = threading.Lock()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="lumen-sync-watchdog-") as profile:
            for case in CASES:
                env = {key: value for key, value in os.environ.items()
                       if not key.startswith(("LUMEN_", "JSRUN_"))}
                env.update(LUMEN_JS_TIMEOUT_MS=str(case["budget"]), LUMEN_PROFILE_DIR=profile,
                           LUMEN_CONSOLE="1", JSRUN_UNTIL_ATTR="data-sync-watchdog-done")
                started = time.monotonic()
                url = "http://127.0.0.1:" + str(server.server_port) + "/?case=" + case["name"]
                error = None
                try:
                    result = subprocess.run([str(binary), url, "1200"], env=env, capture_output=True,
                                            text=True, timeout=8)
                    output = result.stdout + result.stderr
                    exit_code = result.returncode
                except subprocess.TimeoutExpired as exc:
                    stdout = exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else exc.stdout or ""
                    stderr = exc.stderr.decode(errors="replace") if isinstance(exc.stderr, bytes) else exc.stderr or ""
                    output = stdout + stderr
                    exit_code = None
                    error = "Native process exceeded 8 seconds; owned subprocess was killed and reaped"
                markers = []
                for line in output.splitlines():
                    if MARKER in line:
                        try:
                            markers.append(json.loads(line.split(MARKER, 1)[1]))
                        except json.JSONDecodeError:
                            pass
                phases = [item.get("phase") for item in markers]
                after = next((item for item in markers if item.get("phase") == "after_wait"), None)
                diagnostics = re.findall(r"lumen: script exceeded (\d+) ms, terminating", output)
                requests = [dict(row) for row in server.records if row["case"] == case["name"]]
                checks = []

                def check(name, condition):
                    checks.append({"name": name, "passed": bool(condition)})

                check("native process exited normally", exit_code == 0 and error is None)
                check("script started", phases.count("started") == 1)
                check("following classic script survives", phases.count("next_script") == 1)
                check("following task survives stale interrupts", phases.count("next_task") == 1)
                if case["loop"] == "before":
                    check("busy loop terminates before native request", not requests and "before_wait" not in phases)
                else:
                    check("exactly one delayed HTTP response was delivered", len(requests) == 1 and requests[0]["responseWritten"])
                    check("real server waited beyond JS budget", len(requests) == 1 and requests[0].get("waitSeconds", 0) >= DELAY_MS / 1000 - 0.02)
                    check("sync XHR returns the real response", after is not None and after.get("status") == 200 and after.get("body") == "SYNC_WATCHDOG_OK")
                    check("client observed the blocking wait", after is not None and after.get("elapsed", 0) >= DELAY_MS - 100)
                if case["terminate"]:
                    check("watchdog still terminates excess active JavaScript", diagnostics == [str(case["budget"])])
                    check("code after excessive active work is unreachable", "completed" not in phases)
                else:
                    check("native wait does not exhaust active JavaScript budget", not diagnostics)
                    check("all bounded active work completes", phases.count("completed") == 1)
                log_path = args.output.parent / (args.output.stem + "-" + case["name"] + ".log")
                log_path.write_text(output)
                row = {**case, "durationSeconds": round(time.monotonic() - started, 3),
                       "exitCode": exit_code, "error": error, "markers": markers,
                       "watchdogDiagnostics": diagnostics, "requests": requests,
                       "checks": checks, "passed": all(item["passed"] for item in checks),
                       "log": str(log_path), "logSHA256": hashlib.sha256(log_path.read_bytes()).hexdigest()}
                report["cases"].append(row)
                print(("PASS " if row["passed"] else "FAIL ") + case["name"], flush=True)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
        checks = [item for row in report["cases"] for item in row["checks"]]
        report["checksPassed"] = sum(item["passed"] for item in checks)
        report["checksTotal"] = len(checks)
        report["passed"] = len(report["cases"]) == len(CASES) and all(row["passed"] for row in report["cases"])
        report["durationSeconds"] = round(time.monotonic() - began, 3)
        report["finishedUTC"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print("RESULTS sync-xhr-watchdog " + ("PASS" if report["passed"] else "FAIL")
              + " (" + str(report["checksPassed"]) + "/" + str(report["checksTotal"]) + " checks)", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

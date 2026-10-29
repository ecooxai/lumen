#!/usr/bin/env python3
"""Package and launch one verified native Mac build, preserving older bundles.

This creates a local development .app with installed Homebrew dependencies.
It does not create an IPA, claim iOS compatibility, or bypass platform security.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
import plistlib
import struct
import subprocess
import sys
import time

PINNED_TOOLS = {
    "tools/mkdevapp.py": "cdb6080a912ca7056e8c07666dd6c5ad96511a4dd0744cf7208410660b2ab6b4",
    "tools/macos_deployment_closure.py": "1a7bf265eb6fadcf630d8ffc44f173c43e950df9b10cf217176cb0823b8c11c7",
    "tools/monitor_process.py": "46bda36325c6881ceed174ef6fc19fd4745b64894c833717949ea60e4a18d3e1",
}


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-summary", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if str(root) != "/Users/devin/repos/lumen" or sys.platform != "darwin":
        raise SystemExit("This carrier requires the real canonical Mac repository")
    build_path = (root / args.build_summary).resolve()
    build_path.relative_to(root)
    product = json.loads(build_path.read_text())
    if not product.get("passed"):
        raise SystemExit("Native build summary does not pass")
    source_manifest = root / product["compiledSourceManifest"]
    if sha(source_manifest) != product["sourceManifestSHA256"]:
        raise SystemExit("Compiled source manifest differs")
    sources = json.loads(source_manifest.read_text())
    for name, digest in {**sources, **PINNED_TOOLS}.items():
        if sha(root / name) != digest:
            raise SystemExit("Reviewed source or packaging tool differs: " + name)
    artifact = product["artifacts"]["lumen"]
    binary = (root / artifact["path"]).resolve()
    binary.relative_to(root)
    if sha(binary) != artifact["sha256"]:
        raise SystemExit("Immutable native product executable differs")
    out = (root / args.output).resolve()
    out.relative_to(root / "out")
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    env = {key: value for key, value in os.environ.items() if not key.startswith("LUMEN_")}
    env["DEVELOPER_DIR"] = "/Library/Developer/CommandLineTools"
    report = {
        "startedUTC": datetime.now(timezone.utc).isoformat(),
        "platform": "Darwin arm64 native macOS application",
        "scope": "Local development .app, external installed Homebrew dependencies, ad-hoc signature; not IPA, portable distribution or notarized release",
        "sourceBaseCommit": product["sourceBaseCommit"],
        "sourceManifestSHA256": sha(source_manifest),
        "buildSummary": str(build_path.relative_to(root)),
        "buildSummarySHA256": sha(build_path),
        "productArtifact": artifact,
        "reviewedToolHashes": PINNED_TOOLS,
        "stages": [], "passed": False,
    }

    def save():
        (out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")

    def measured(name, command, limit, environment=env):
        prefix = out / name
        result = subprocess.run([
            sys.executable, "tools/monitor_process.py", "--duration", str(limit),
            "--settle", "0", "--output", str(prefix.relative_to(root)), "--", *command,
        ], env=environment, capture_output=True, text=True, timeout=limit + 35)
        (out / (name + "-collector.log")).write_text(result.stdout + result.stderr)
        data = json.loads(prefix.with_suffix(".json").read_text())
        okay = (data.get("natural_exit_code") == 0 and not data.get("collector_error")
                and not data.get("ended_by_monitor")
                and not data.get("owned_group_cleanup", {}).get("error"))
        row = {"name": name, "passed": okay, "command": command,
               "durationSeconds": data.get("duration_s"),
               "monitor": str(prefix.with_suffix(".json").relative_to(root)),
               "monitorSHA256": sha(prefix.with_suffix(".json")),
               "logSHA256": sha(prefix.with_suffix(".log")),
               "maxSamplingGapSeconds": data.get("max_sampling_gap_s"),
               "intervalCPU_P95Percent": data.get("steady_interval_cpu_p95_percent"),
               "intervalCPU_PeakPercent": data.get("interval_cpu_peak_percent"),
               "cpuUnit": data.get("cpu_unit"), "rssPeakMiB": data.get("rss_peak_mb")}
        report["stages"].append(row)
        save()
        if not okay:
            raise RuntimeError(name + " failed; raw monitor and log preserved")
        return data

    try:
        template = root / "out/GPT6-Astra-Pro-browser-engine-devinmac3d-Lumen-37447be5593f.app"
        report["priorBundleInspection"] = {"path": str(template.relative_to(root)), "exists": template.is_dir(), "modified": False}
        if template.is_dir():
            info = plistlib.loads((template / "Contents/Info.plist").read_bytes())
            prior_executable = template / "Contents/MacOS" / info["CFBundleExecutable"]
            verify = subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(template)],
                                    env=env, capture_output=True, text=True, timeout=15)
            report["priorBundleInspection"].update(
                bundleIdentifier=info.get("CFBundleIdentifier"),
                minimumSystemVersion=info.get("LSMinimumSystemVersion"),
                executableSHA256=sha(prior_executable), signatureVerificationExitCode=verify.returncode)
            if verify.returncode:
                raise RuntimeError("Previously inspected development bundle no longer verifies")
        delivery = out / "delivery"
        delivery.mkdir()
        app = delivery / "GPT6-Astra-Pro_builtin-agents_devinmac3d_Lumen-10.292019r.app"
        measured("package", [sys.executable, "tools/mkdevapp.py", "--output", str(app),
                  "--binary", str(binary), "--expected-binary-sha256", artifact["sha256"],
                  "--identifier", "ai.ecoox.lumen.gpt6as.10292019r"], 120)
        lines = (out / "package.log").read_text().splitlines()
        metadata = next(json.loads(line) for line in reversed(lines) if line.startswith('{"app":'))
        if (metadata.get("binaryBeforeBundleSigningSha256") != artifact["sha256"]
                or not metadata.get("expectedBinarySha256Verified")
                or not metadata.get("deploymentClosure", {}).get("nonSystemClosureFullyResolved")):
            raise RuntimeError("Package source bytes or dependency closure were not verified")
        report["package"] = metadata
        executable = Path(metadata["executable"])
        if sha(executable) != metadata["binarySha256"]:
            raise RuntimeError("Signed bundle executable differs before launch")
        fixture = out / "package-launch.html"
        fixture.write_text("""<!doctype html><html><head><meta charset="utf-8"><title>Lumen 10.292019r</title>
<style>body{background:#edf6ff;color:#142d48;font:20px system-ui;margin:40px}h1{font-size:36px}p{line-height:1.6}</style></head>
<body><h1>Lumen 10.292019r</h1><p>Native Mac development application</p><p id="status">Checking runtime…</p>
<script>try {let decoded=new TextDecoder().decode(new Uint8Array([76,117,109,101,110]));
let formatted=new Intl.DateTimeFormat('en-US',{timeZone:'UTC'}).format(new Date(0));
if(decoded!=='Lumen'||!formatted)throw Error('Runtime validation failed');
document.getElementById('status').textContent='C/V8 runtime, HTML, text decoding and ICU: ready';
console.log('LUMEN_MAC_APP_PACKAGE_READY');}catch(e){console.error('LUMEN_MAC_APP_PACKAGE_FAILED',String(e));}</script></body></html>""")
        profile = out / "launch-profile"
        profile.mkdir()
        screenshot = out / "gpt6-astra-pro_builtin-agents_devinmac3d_mac-app.png"
        launch_env = dict(env)
        launch_env.update(LUMEN_PROFILE_DIR=str(profile), LUMEN_CONSOLE="1", LUMEN_NO_SIDEBAR="1",
                          LUMEN_WINDOW="1000x700", LUMEN_MEM_STATS="1", LUMEN_SHOT_MS="15000",
                          LUMEN_SHOT=str(screenshot))
        measured("launch", [str(executable), fixture.as_uri()], 35, launch_env)
        log = (out / "launch.log").read_text(errors="replace")
        errors = [line for line in log.splitlines() if any(term in line for term in (
            "LUMEN_MAC_APP_PACKAGE_FAILED", "gpu: error", "Validation Error", "fatal signal", "panicked at", "panic in a function"))]
        if "LUMEN_MAC_APP_PACKAGE_READY" not in log or errors:
            raise RuntimeError("Packaged runtime did not prove successful HTML/V8/ICU execution: " + str(errors[:3]))
        header = screenshot.read_bytes()[:33]
        if len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
            raise RuntimeError("Packaged application did not produce its owned render capture")
        width, height = struct.unpack(">II", header[16:24])
        report["launch"] = {"mode": "Direct owned .app executable; not Finder/LaunchServices automation",
                            "durationRequestedSeconds": 15, "runtimeMarkerObserved": True,
                            "renderCapture": str(screenshot.relative_to(root)),
                            "renderCaptureSHA256": sha(screenshot), "width": width, "height": height,
                            "renderCaptureIsOSWindowScreenshot": False}
        measured("verify", ["/usr/bin/codesign", "--verify", "--strict", str(app)], 20)
        (delivery / "README-Mac-development.txt").write_text(
            "Lumen 10.292019r — GPT-6 Astra Pro / built-in agents / devinmac3d\n\n"
            "This is a native arm64 macOS DEVELOPMENT application, tested on the source Mac.\n"
            "It requires the external installed Homebrew libraries recorded in dependency-closure.json.\n"
            "It uses an ad-hoc local signature, is not notarized, and is not an iOS/iPad IPA.\n"
            "No gatekeeper, AMFI, entitlement or provisioning bypass is included.\n"
            "Minimum macOS declared by actual dependency closure: " + metadata["minimumSystemVersion"] + "\n"
            "The minimum is a link-time declaration, not an older-macOS runtime test.\n"
            "Product source commit: " + product["sourceBaseCommit"] + "\n"
            "Original tested executable SHA-256: " + artifact["sha256"] + "\n"
            "Signed bundle executable SHA-256: " + metadata["binarySha256"] + "\n")
        (delivery / "dependency-closure.json").write_text(json.dumps(metadata, indent=2) + "\n")
        archive = out / "GPT6-Astra-Pro_builtin-agents_devinmac3d_Lumen-10.292019r-Mac-development.zip"
        measured("archive", ["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(delivery), str(archive)], 90)
        report["archive"] = {"path": str(archive.relative_to(root)), "bytes": archive.stat().st_size, "sha256": sha(archive)}
        if sha(binary) != artifact["sha256"] or sha(executable) != metadata["binarySha256"]:
            raise RuntimeError("Product or signed bundle changed during acceptance")
        if template.is_dir() and sha(prior_executable) != report["priorBundleInspection"]["executableSHA256"]:
            raise RuntimeError("Prior bundle changed during packaging")
        for name, digest in sources.items():
            if sha(root / name) != digest:
                raise RuntimeError("Product source changed during packaging: " + name)
        report["passed"] = True
    except Exception as error:
        report["error"] = type(error).__name__ + ": " + str(error)
    finally:
        report["finishedUTC"] = datetime.now(timezone.utc).isoformat()
        report["durationSeconds"] = round(time.monotonic() - started, 3)
        save()
        print(json.dumps({"passed": report["passed"], "summary": str((out / "summary.json").relative_to(root)),
                          "archive": report.get("archive"), "durationSeconds": report["durationSeconds"],
                          "error": report.get("error")}), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

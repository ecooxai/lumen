#!/usr/bin/env python3
"""Install and verify the completed signed build for the one authorized iPad.

The preceding build must pass its retained monitor report. Installation uses
the same one-second process monitor. This launcher never builds, boots, shuts
down, erases, launches, or terminates an app/device.
"""
import argparse
import fcntl
import hashlib
import importlib
import json
from pathlib import Path
import platform
import plistlib
import subprocess
import sys
import time


DEVICE = "A77F68E4-972B-472B-80B8-F65E71D361E5"
BUNDLE = "com.ecooxai.lumen"
SOURCES = {"ipad_runner.py": "1876472b3552a425f9539ef9bb638cf86a6e006c3bc5f6ca603eb8305a39c4ba",
           "ipad_adapter.py": "0a45a31740340bf321c7add11dcaa1ffca8a63de44261ff7290f2a8413258c34",
           "full_runner.py": "a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7",
           "ipad_generation.py": "38d6f962059e932e12a158ae94df37ca289f434380e6478b1ede856b31fafb73"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def preinstall_state(adapter_module):
    """Read exact device/registration/process state without requiring a valid app."""
    inventory = subprocess.run(["xcrun", "simctl", "list", "devices", "--json"],
                               capture_output=True, text=True, timeout=20, check=True)
    target = adapter_module.select_device(json.loads(inventory.stdout), DEVICE)
    require(target["runtime"] == "com.apple.CoreSimulator.SimRuntime.iOS-26-5"
            and target["device_type"] == "com.apple.CoreSimulator.SimDeviceType.iPad-Air-11-inch-M4"
            and target["name"] == "Lumen gpt-6-astra-pro devinmac3d iPad 20261022-004",
            "The newly created iPad identity changed")
    # This data root is observed on the authorized host. Never infer another
    # simulator or delete a stale registration/container to make installation work.
    data_root = (Path("/Users/devin/Library/Developer/CoreSimulator/Devices") / DEVICE / "data").resolve(strict=True)
    require(DEVICE in data_root.parts, "Authorized simulator data root identity changed")
    app_root = data_root / "Containers/Bundle/Application"
    lookup = subprocess.run(["xcrun", "simctl", "get_app_container", DEVICE, BUNDLE, "app"],
                            capture_output=True, text=True, timeout=20)
    registered = None
    if lookup.returncode == 0:
        require(bool(lookup.stdout.strip()), "simctl returned an empty registered app path")
        registered = Path(lookup.stdout.strip()).resolve()
        require(registered.is_relative_to(app_root) and registered.name == "Lumen.app",
                "Registered Lumen app path escaped the exact simulator bundle root")
    else:
        # A missing registration is a legitimate first install. Transport/device
        # failures remain errors; they cannot be mistaken for an absent app.
        require("domain=NSPOSIXErrorDomain, code=2" in lookup.stderr,
                "Cannot inspect the existing Lumen registration: " + lookup.stderr[-2000:])
    process_list = subprocess.run(["ps", "-A", "-ww", "-o", "pid=,comm="],
                                  capture_output=True, text=True, timeout=5, check=True)
    running = []
    for line in process_list.stdout.splitlines():
        parts = line.split(None, 1)
        if len(parts) != 2 or not parts[0].isdigit():
            continue
        executable = Path(parts[1].strip()).resolve()
        if not executable.is_relative_to(app_root):
            continue
        if "Lumen.app" in executable.parts or (registered is not None and executable.is_relative_to(registered)):
            running.append({"pid": int(parts[0]), "executable": str(executable)})
    return {"device": target, "simulator_data_root": str(data_root),
            "registered_app": str(registered) if registered is not None else None,
            "registered_path_exists": registered.exists() if registered is not None else False,
            "registered_info_exists": (registered / "Info.plist").is_file() if registered is not None else False,
            "registration_exit_code": lookup.returncode,
            "registration_error": lookup.stderr.strip()[:2000] or None,
            "running_instances": running}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="fresh directory under output/wpt")
    parser.add_argument("--diagnose-only", action="store_true",
                        help="Record device/registration/idle/source-signature evidence without installing")
    args = parser.parse_args()
    require(platform.system() == "Darwin", "Execute only on the authorized Mac through devinmac3d")
    root = args.root.resolve(strict=True)
    require(root == Path("/Users/devin/repos/lumen"), "Use only the authorized Mac repository")
    output = (root / args.output).resolve()
    build_report = (root / args.build_report).resolve(strict=True)
    require(output.is_relative_to(root / "output/wpt"), "Evidence must stay under this project's output/wpt")
    require(build_report.is_relative_to(root / "output/wpt"), "Build report must be project-local WPT evidence")
    built = json.loads(build_report.read_text())
    require(built.get("natural_exit_code") == 0 and built.get("ended_by_monitor") is False
            and not built.get("collector_error") and not built.get("owned_group_cleanup", {}).get("error")
            and not built.get("violations"), "Completed build has not passed its monitored completion gate")
    source_dir = root / "tests/wpt-ipad004"
    for name, digest in SOURCES.items():
        require(hashlib.sha256((source_dir / name).read_bytes()).hexdigest() == digest,
                "Reviewed iPad source changed: " + name)
    sys.path.insert(0, str(source_dir))
    generation = importlib.import_module("ipad_generation")
    binding = generation.verify_generation(root)
    require(build_report == root / generation.BUILD_REPORT, "Use the exact proven build003 report")
    require(output.is_relative_to(root / generation.GENERATION), "New iPad evidence must stay in generation004")
    entry = importlib.import_module("ipad_runner")
    adapter_module = importlib.import_module("ipad_adapter")
    core = importlib.import_module("full_runner")
    require(entry.AUTHORIZED_DEVICE == DEVICE and entry.AUTHORIZED_BUNDLE == BUNDLE,
            "Authorized iPad identity changed")
    output.mkdir(parents=True, exist_ok=False)
    report = {"status": "running", "started_unix": time.time(), "device": DEVICE, "bundle_id": BUNDLE,
              "build_report": str(build_report.relative_to(root)), "build_report_sha256": core.file_digest(build_report),
              "generation_binding": binding, "reviewed_sources": SOURCES, "core_sha256": core.file_digest(Path(core.__file__)),
              "monitor_sha256": core.file_digest(root / "tools/monitor_process.py"),
              "scope": "Signed iPad Simulator install identity; no browser/WPT or same-IPA-on-Mac claim",
              "steps": []}

    def save():
        core.write_json(output / "install-verify.json", report)

    def idle_adapter():
        adapter = entry.IpadAdapter(DEVICE, BUNDLE, runtime=core)
        require(not adapter._running_instances(),
                "Lumen is already running on this exact iPad; retain that instance and inspect before installing")
        return adapter

    def monitored(name, duration, command):
        report["phase"] = name
        save()
        prefix = output / name
        invocation = [sys.executable, str(root / "tools/monitor_process.py"), "--duration", str(duration),
                      "--settle", "0", "--output", str(prefix), "--", *command]
        completed = subprocess.run(invocation, cwd=root, check=False)
        measurement = json.loads(prefix.with_suffix(".json").read_text())
        record = {"name": name, "command": command, "monitor_exit_code": completed.returncode,
                  "measurement": str(prefix.with_suffix(".json").relative_to(root)),
                  "natural_exit_code": measurement.get("natural_exit_code"),
                  "ended_by_monitor": measurement.get("ended_by_monitor"),
                  "collector_error": measurement.get("collector_error"),
                  "owned_group_cleanup": measurement.get("owned_group_cleanup"),
                  "violations": measurement.get("violations", [])}
        report["steps"].append(record)
        save()
        require(completed.returncode == 0 and measurement.get("natural_exit_code") == 0
                and measurement.get("ended_by_monitor") is False
                and not measurement.get("collector_error")
                and not measurement.get("owned_group_cleanup", {}).get("error")
                and not measurement.get("violations"),
                name + " failed its monitored completion gate; inspect retained JSON/log")

    code = 1
    lock_path = root / "output/wpt" / ("ipad-" + DEVICE + ".lock")
    try:
        with lock_path.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            prior = preinstall_state(adapter_module)
            report["preinstall_state"] = prior
            core.write_json(output / "authorized-device.json", prior["device"])
            save()
            require(not prior["running_instances"],
                    "A Lumen process is active on this exact iPad; inspect it before installation")
            binary = entry.signed_binary(root)
            app = (root / "build-sim/Lumen.app").resolve(strict=True)
            require(app.is_relative_to(root) and binary.parent == app,
                    "Source signed app must resolve inside this repository with its executable at the bundle root")
            guards = {str(app): entry.bundle_tree(app)}
            with (app / "Info.plist").open("rb") as source_info:
                info = plistlib.load(source_info)
            require(info.get("CFBundleIdentifier") == BUNDLE and info.get("CFBundleExecutable") == "Lumen",
                    "Source app does not identify the authorized Lumen bundle/executable")
            require("iPhoneSimulator" in info.get("CFBundleSupportedPlatforms", []),
                    "Source app does not declare the simulator platform")
            report["source_bundle_identity"] = {key: info[key] for key in
                ("CFBundleIdentifier", "CFBundleExecutable", "CFBundleSupportedPlatforms")}
            entry.check_bundle_guards(guards)
            with (output / "source-signature-verify.log").open("w") as log:
                subprocess.run(["codesign", "--verify", "--strict", "--deep", str(app)],
                               cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=60, check=True)
            with (output / "source-signature.txt").open("w") as log:
                subprocess.run(["codesign", "-dv", "--verbose=4", str(app)],
                               cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=30, check=True)
            entry.check_bundle_guards(guards)
            report["source_executable_sha256"] = core.file_digest(binary)
            require(report["source_executable_sha256"] == generation.BINARY_SHA,
                    "Signed source no longer matches the proven build003 executable")
            final_idle = preinstall_state(adapter_module)
            report["preinstall_state_final"] = final_idle
            save()
            require(not final_idle["running_instances"] and
                    final_idle["registered_app"] == prior["registered_app"],
                    "Lumen's process/registration state changed during source validation")
            entry.check_bundle_guards(guards)
            if args.diagnose_only:
                report.update(status="preinstall_diagnostics_complete", phase="read_only_diagnostics")
            else:
                require(generation.verify_generation(root) == binding, "Generation inputs changed before install")
                monitored("simulator-install", 120,
                          ["xcrun", "simctl", "install", DEVICE, str(binary.parent)])
                entry.check_bundle_guards(guards)
                installed = idle_adapter()
                installed._verify_binary(binary)
                proof, final_guards = entry.verify_signed_bundles(binary.parent, installed.app)
                entry.check_bundle_guards(final_guards)
                require(generation.verify_generation(root) == binding, "Generation inputs changed during install")
                core.write_json(output / "installed-identity.json",
                                {"adapter": installed.metadata, "signed_bundle_provenance": proof})
                report.update(status="installed_and_verified", phase="complete",
                              installed_executable_sha256=installed.metadata["installed_binary_sha256"],
                              matching_bundle_contents_sha256=proof["matching_bundle_contents_sha256"])
            code = 0
    except Exception as error:
        report.update(status="stopped", error=type(error).__name__ + ": " + str(error))
    finally:
        report["finished_unix"] = time.time()
        save()
    print(json.dumps({key: report.get(key) for key in
                      ("status", "phase", "error", "installed_executable_sha256")}))
    return code


if __name__ == "__main__":
    raise SystemExit(main())

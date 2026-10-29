#!/usr/bin/env python3
"""Relocate an inspected native macOS dylib closure into a new development app.

Only copies in the output bundle are rewritten or ad-hoc signed. The original
tested executable and the installed prebuilt dependencies remain immutable.
This is linked dependency closure, not notarization or proof on another Mac.
"""
from pathlib import Path
import re
import shutil
import subprocess
import time

from macos_deployment_closure import inspect_closure, parse_commands, sha256


def system_install_name(name):
    return (isinstance(name, str) and name.startswith(("/System/Library/", "/usr/lib/"))
            and not any(part in (".", "..") for part in name.split("/")))


def plan_bundle(closure, app):
    if closure.get("nonSystemClosureFullyResolved") is not True:
        raise ValueError("A complete inspected dependency closure is required")
    app = Path(app).resolve()
    executable = Path(closure["executable"]).resolve()
    entries, names, targets = {}, {}, {}
    for entry in closure["files"]:
        source = Path(entry["path"]).resolve()
        if source in entries:
            raise ValueError("Duplicate source in dependency closure")
        entries[source] = entry
        if source == executable:
            targets[source] = app / "Contents/MacOS/lumen"
            continue
        name = source.name
        if not re.fullmatch(r"[A-Za-z0-9_.+-]+\.dylib", name):
            raise ValueError("This native app packager requires flat dylib dependencies: " + name)
        if name.casefold() in names:
            raise ValueError("Dylib filename collision; refusing to merge different source libraries: " + name)
        names[name.casefold()] = source
        targets[source] = app / "Contents/Frameworks" / name
    if executable not in entries:
        raise ValueError("The source executable is missing from its dependency closure")
    plan = []
    for source, entry in entries.items():
        changes = []
        for edge in entry["dependencies"]:
            old = edge["installName"]
            if edge.get("systemSharedCache"):
                if not system_install_name(old):
                    raise ValueError("A system dependency requires a canonical system install name")
                continue
            if not isinstance(old, str) or not old or not edge.get("resolved"):
                raise ValueError("A dependency edge is missing its resolved target")
            resolved = Path(edge["resolved"]).resolve()
            if resolved not in targets:
                raise ValueError("A transitive dependency is missing from the inspected closure")
            prefix = "@executable_path/../Frameworks/" if source == executable else "@loader_path/"
            changes.append([old, prefix + targets[resolved].name])
        plan.append({"source": str(source), "target": str(targets[source]),
                     "sourceSha256": entry["sha256"], "isExecutable": source == executable,
                     "changes": changes})
    return plan


def require_embedded_closure(closure, app):
    if closure.get("nonSystemClosureFullyResolved") is not True or not closure.get("files"):
        raise ValueError("The signed bundle has no complete dependency closure")
    app = Path(app).resolve()
    for entry in closure["files"]:
        if not Path(entry["path"]).resolve().is_relative_to(app):
            raise ValueError("A non-system runtime dependency remains outside the app: " + entry["path"])


def verify_recorded_bundle(closure, app, resources):
    """Bind subsequent GUI gates to every delivered image and data resource."""
    require_embedded_closure(closure, app)
    app = Path(app).resolve()
    for entry in closure["files"]:
        if sha256(Path(entry["path"])) != entry["sha256"]:
            raise ValueError("A recorded bundle image changed: " + Path(entry["path"]).name)
    for entry in resources:
        name = entry["name"]
        if not isinstance(name, str) or name in ("", ".", "..") or Path(name).name != name:
            raise ValueError("A recorded bundle resource has an invalid name")
        path = (app / "Contents/Resources" / name).resolve()
        if not path.is_relative_to(app) or sha256(path) != entry["sha256"]:
            raise ValueError("A recorded bundle resource changed: " + name)
    return {"imagesVerified": len(closure["files"]), "resourcesVerified": len(resources)}


def command_output(command):
    result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True,
                            text=True, timeout=20, check=False)
    if result.returncode:
        raise RuntimeError("Native packaging command failed: " + command[0] + ": " + result.stderr[-2000:])
    return result.stdout


def embed_dependencies(project, closure, app):
    started = time.monotonic()
    app = Path(app).resolve()
    plan = plan_bundle(closure, app)
    frameworks = app / "Contents/Frameworks"
    frameworks.mkdir(exist_ok=False)
    for row in plan:
        source, target = Path(row["source"]), Path(row["target"])
        if sha256(source) != row["sourceSha256"]:
            raise RuntimeError("A prebuilt dependency changed before copying: " + source.name)
        if not row["isExecutable"]:
            shutil.copy2(source, target)
            target.chmod(0o755)
        if sha256(target) != row["sourceSha256"]:
            raise RuntimeError("A bundled dependency copy does not match its inspected source: " + source.name)
    for row in plan:
        target = Path(row["target"])
        _, _, rpaths = parse_commands(command_output(["/usr/bin/otool", "-arch", "arm64", "-l", str(target)]))
        command = ["/usr/bin/install_name_tool"]
        if not row["isExecutable"]:
            command += ["-id", "@rpath/" + target.name]
        for old, new in row["changes"]:
            command += ["-change", old, new]
        # Every non-system load is now explicit and bundle-relative. Retaining
        # host search paths would hide accidental dependencies during testing.
        for rpath in dict.fromkeys(rpaths):
            command += ["-delete_rpath", rpath]
        if len(command) > 1:
            command_output(command + [str(target)])
        if not row["isExecutable"]:
            command_output(["/usr/bin/codesign", "--force", "--timestamp=none", "--sign", "-", str(target)])
            command_output(["/usr/bin/codesign", "--verify", "--strict", str(target)])
        row["bundledSha256"] = sha256(target)
    executable = app / "Contents/MacOS/lumen"
    embedded = inspect_closure(project, executable)
    require_embedded_closure(embedded, app)
    if len(embedded["files"]) != len(plan):
        raise RuntimeError("The relocated dependency graph differs from the source closure")
    for row in plan:
        if sha256(Path(row["source"])) != row["sourceSha256"]:
            raise RuntimeError("An installed dependency changed during packaging")
    return {"schemaVersion": 1, "linkedClosureEmbedded": True, "dylibCount": len(plan) - 1,
            "originalSourcesUnchanged": True, "images": plan,
            "deploymentClosure": embedded, "elapsedSeconds": round(time.monotonic() - started, 3)}

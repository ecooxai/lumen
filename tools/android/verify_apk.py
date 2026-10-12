#!/usr/bin/env python3
"""Check Android APK native dependencies and ELF layout, not runtime behavior."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile


MACHINES = {"arm64-v8a": 183, "x86_64": 62}
REQUIRED_LIBRARIES = {"libmain.so", "libSDL3.so", "libwgpu_native.so"}
MAX_LIBRARY_BYTES = 256 * 1024 * 1024
MAX_TOTAL_BYTES = 1024 * 1024 * 1024


def read_tool(arguments):
    result = subprocess.run(arguments, check=True, capture_output=True, text=True,
                            timeout=60, env={**os.environ, "LC_ALL": "C"})
    return result.stdout


def library_name(name):
    return re.fullmatch(r"lib[A-Za-z0-9_+.-]+\.so", name) is not None


def inspect_library(path, readelf, abi):
    if abi not in MACHINES:
        raise ValueError(f"Unsupported ABI: {abi}")
    report = json.loads(read_tool([str(readelf), "--elf-output-style=JSON",
                                   "--file-header", "--program-headers", str(path)]))
    if len(report) != 1:
        raise ValueError(f"Expected one ELF: {path.name}")
    header = report[0]["ElfHeader"]
    if (header["Machine"]["Value"] != MACHINES[abi] or
            header["Ident"]["Class"]["Value"] != 2 or
            header["Ident"]["DataEncoding"]["Value"] != 1 or
            header["Type"] != "SharedObject (0x3)"):
        raise ValueError(f"Wrong ELF architecture/type for {abi}: {path.name}")
    segments = [entry["ProgramHeader"] for entry in report[0]["ProgramHeaders"]
                if entry["ProgramHeader"]["Type"]["Value"] == 1]
    if not segments:
        raise ValueError(f"No LOAD segments: {path.name}")
    for segment in segments:
        alignment = segment["Alignment"]
        if (alignment < 16384 or alignment & (alignment - 1) or
                (segment["VirtualAddress"] - segment["Offset"]) % alignment):
            raise ValueError(f"Invalid 16 KiB LOAD alignment: {path.name}")
    dynamic = read_tool([str(readelf), "--dynamic-table", "--wide", str(path)])
    if re.search(r"\b(?:RPATH|RUNPATH|TEXTREL)\b", dynamic):
        raise ValueError(f"Unsafe dynamic search path or text relocations: {path.name}")
    sonames = re.findall(r"\bSONAME\)?\s+Library soname: \[([^\]]*)\]", dynamic)
    if sonames != [path.name]:
        raise ValueError(f"Missing or mismatched SONAME: {path.name}")
    dependencies = re.findall(r"\bNEEDED\)?\s+Shared library: \[([^\]]*)\]", dynamic)
    if any(not library_name(name) for name in dependencies):
        raise ValueError(f"Non-library dependency name: {path.name}")
    return dependencies


def verify_apk(apk, readelf, system_libraries, expected_abis, min_api=28, work_dir=None):
    if not expected_abis or not set(expected_abis) <= MACHINES.keys():
        raise ValueError("Expected ABI set must contain arm64-v8a and/or x86_64")
    if min_api < 28:
        raise ValueError("Lumen requires Android API 28 or newer")
    system = {name for name, since in system_libraries.items() if int(since) <= min_api}
    result = {}
    with zipfile.ZipFile(apk) as archive:
        members = {}
        total_size = 0
        for entry in archive.infolist():
            if not entry.filename.startswith("lib/") or entry.is_dir():
                continue
            parts = entry.filename.split("/")
            if len(parts) != 3 or parts[1] not in MACHINES or not library_name(parts[2]):
                raise ValueError(f"Invalid native APK member: {entry.filename}")
            if entry.filename in members:
                raise ValueError(f"Duplicate native APK member: {entry.filename}")
            if parts[2] in system_libraries:
                raise ValueError(f"Packaged Android system library: {entry.filename}")
            total_size += entry.file_size
            if not 0 < entry.file_size <= MAX_LIBRARY_BYTES or total_size > MAX_TOTAL_BYTES:
                raise ValueError("Native library size limit exceeded")
            members[entry.filename] = entry
            result.setdefault(parts[1], {})[parts[2]] = []
        if set(result) != set(expected_abis):
            raise ValueError(f"APK ABI set {sorted(result)} differs from {sorted(expected_abis)}")
        for abi, libraries in result.items():
            missing = REQUIRED_LIBRARIES - libraries.keys()
            if missing:
                raise ValueError(f"Missing {abi} libraries: {', '.join(sorted(missing))}")
        with tempfile.TemporaryDirectory(prefix="codex-apk-native-",
                                         dir=work_dir or Path(apk).resolve().parent) as temporary:
            for member, entry in members.items():
                _, abi, name = member.split("/")
                path = Path(temporary) / name
                with archive.open(entry) as source, path.open("wb") as destination:
                    shutil.copyfileobj(source, destination, length=1024 * 1024)
                result[abi][name] = inspect_library(path, readelf, abi)
                path.unlink()
        for abi, libraries in result.items():
            available = libraries.keys() | system
            for name, dependencies in libraries.items():
                missing = set(dependencies) - available
                if missing:
                    raise ValueError(f"{abi}/{name} unresolved dependencies at API {min_api}: "
                                     f"{', '.join(sorted(missing))}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--readelf", type=Path)
    parser.add_argument("--abi", action="append", choices=sorted(MACHINES))
    parser.add_argument("--min-api", type=int, default=28)
    parser.add_argument("--work-dir", type=Path)
    arguments = parser.parse_args()
    readelf = arguments.readelf or arguments.ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
    try:
        system_libraries = json.loads((arguments.ndk / "meta/system_libs.json").read_text())
        result = verify_apk(arguments.apk, readelf, system_libraries,
                            set(arguments.abi or ["arm64-v8a"]), arguments.min_api, arguments.work_dir)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile,
            subprocess.SubprocessError) as error:
        parser.exit(1, f"Native APK verification failed: {error}\n")
    print(json.dumps({"native_libraries": result, "min_api": arguments.min_api,
                      "scope": "Native ELF layout and dependency closure only; not APK signature, "
                               "manifest, ZIP alignment, symbol/API compatibility or runtime validation."}, indent=2))


if __name__ == "__main__":
    main()

import argparse
import json
import os
from pathlib import Path
import subprocess


VCPKG_REVISION = "e456309491fd875487bf02b8a0dca76c1b1b00cc"
SDL_REVISION = "8e37db5e797b6167f3a00d697d816a684bd259c7"
NDK_VERSION = "28.2.13676358"
TARGETS = {
    "arm64-v8a": ("arm64-android", "aarch64-linux-android28-clang"),
    "x86_64": ("x64-android", "x86_64-linux-android28-clang"),
}


def validate_ndk(ndk, abi):
    properties = ndk / "source.properties"
    if not properties.is_file():
        raise ValueError(f"NDK source.properties missing: {ndk}")
    values = dict(line.split("=", 1) for line in properties.read_text().splitlines() if "=" in line)
    values = {key.strip(): value.strip() for key, value in values.items()}
    if values.get("Pkg.Revision") != NDK_VERSION:
        raise ValueError(f"Expected NDK {NDK_VERSION}, found {values.get('Pkg.Revision')}")
    compiler = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin" / TARGETS[abi][1]
    if not compiler.is_file() or not os.access(compiler, os.X_OK):
        raise ValueError(f"Android compiler missing or not executable: {compiler}")


def build_plan(root, work, ndk, abi, jobs):
    if abi not in TARGETS or not 1 <= jobs <= 64:
        raise ValueError("Select arm64-v8a or x86_64 and 1–64 build jobs")
    root, work, ndk = root.resolve(), work.resolve(), ndk.resolve()
    triplet = TARGETS[abi][0]
    vcpkg = work / "vcpkg"
    sdl = work / "SDL"
    manifest = root / "android/native-deps"
    steps = []
    for directory, remote, revision in [
        (vcpkg, "https://github.com/microsoft/vcpkg.git", VCPKG_REVISION),
        (sdl, "https://github.com/libsdl-org/SDL.git", SDL_REVISION),
    ]:
        steps.extend([
            {"argv": ["git", "clone", "--filter=blob:none", remote, str(directory)], "unless_exists": str(directory / ".git")},
            {"argv": ["git", "-C", str(directory), "diff", "--quiet"]},
            {"argv": ["git", "-C", str(directory), "diff", "--cached", "--quiet"]},
            {"argv": ["git", "-C", str(directory), "fetch", "origin", revision]},
            {"argv": ["git", "-C", str(directory), "checkout", "--detach", revision]},
        ])
    steps.extend([
        {"argv": [str(vcpkg / "bootstrap-vcpkg.sh"), "-disableMetrics"]},
        {"argv": [str(vcpkg / "vcpkg"), "install", "--triplet", triplet,
                  f"--x-manifest-root={manifest}", f"--x-install-root={work / 'installed'}",
                  f"--overlay-triplets={manifest / 'triplets'}", "--disable-metrics"]},
    ])
    return {
        "abi": abi,
        "prefix": str(work / "prefix" / abi),
        "installed": str(work / "installed" / triplet),
        "sdl": str(sdl),
        "environment": {
            "ANDROID_NDK_HOME": str(ndk),
            "ANDROID_NDK_ROOT": str(ndk),
            "VCPKG_ROOT": str(vcpkg),
            "VCPKG_MAX_CONCURRENCY": str(jobs),
            "VCPKG_DISABLE_METRICS": "1",
            "VCPKG_DOWNLOADS": str(work / "downloads"),
            "TMPDIR": str(work / "tmp"),
        },
        "steps": steps,
        "remaining": ["V8", "wgpu-native", "engine link and APK build", "device tests"],
    }


def execute(plan, ndk):
    validate_ndk(ndk.resolve(), plan["abi"])
    environment = dict(os.environ, **plan["environment"])
    for name in ("TMPDIR", "VCPKG_DOWNLOADS"):
        Path(environment[name]).mkdir(parents=True, exist_ok=True)
    for step in plan["steps"]:
        if step.get("unless_exists") and Path(step["unless_exists"]).exists():
            continue
        print(json.dumps(step["argv"]), flush=True)
        subprocess.run(step["argv"], env=environment, check=True)
    installed = Path(plan["installed"])
    if not (installed / "include").is_dir() or not (installed / "lib").is_dir():
        raise ValueError(f"Build did not produce a dependency prefix: {installed}")
    prefix = Path(plan["prefix"])
    prefix.parent.mkdir(parents=True, exist_ok=True)
    if prefix.is_symlink() or prefix.exists():
        if prefix.resolve() != installed.resolve():
            raise ValueError(f"Refusing to replace an unrelated prefix: {prefix}")
    else:
        prefix.symlink_to(installed, target_is_directory=True)
    print(f"Android foundation dependencies: {prefix}")
    print("Still required: " + ", ".join(plan["remaining"]))


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description="Build pinned Android foundation dependencies; does not produce an APK.")
    parser.add_argument("--ndk", type=Path, required=True)
    parser.add_argument("--abi", choices=TARGETS, default="arm64-v8a")
    parser.add_argument("--work-dir", type=Path, default=root / "build/codex-android-deps")
    parser.add_argument("--jobs", type=int, default=2)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--execute", action="store_true")
    mode.add_argument("--dry-run", action="store_true")
    arguments = parser.parse_args()
    try:
        plan = build_plan(root, arguments.work_dir, arguments.ndk, arguments.abi, arguments.jobs)
        if arguments.execute:
            execute(plan, arguments.ndk)
        else:
            print(json.dumps(plan, indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Android dependency build failed: {error}\n")


if __name__ == "__main__":
    main()

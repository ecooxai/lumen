"""Verify the pinned Android V8 SDK before importing it into a native build."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys


MANIFEST = Path(__file__).resolve().parents[2] / "android/native-deps/v8-prebuilt.json"


def file_digest(path):
    if path.is_symlink():
        raise ValueError(f"SDK symlink is not allowed: {path}")
    if not path.is_file():
        raise ValueError(f"Missing SDK file: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_digest(root):
    if root.is_symlink():
        raise ValueError(f"SDK symlink is not allowed: {root}")
    if not root.is_dir():
        raise ValueError(f"Missing SDK directory: {root}")
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"SDK symlink is not allowed: {path}")
        if path.is_dir():
            continue
        entry = path.relative_to(root).as_posix() + "\0" + file_digest(path) + "\n"
        digest.update(entry.encode("utf-8"))
    return digest.hexdigest()


def sdk_path(sdk, name):
    relative = PurePosixPath(name)
    if not name or relative.is_absolute() or ".." in relative.parts or str(relative) != name:
        raise ValueError(f"Invalid SDK path: {name}")
    path = sdk
    for part in relative.parts:
        path = path / part
        if path.is_symlink():
            raise ValueError(f"SDK symlink is not allowed: {path}")
    return path


def verify_sdk(sdk, manifest, abi, api):
    if manifest.get("schema") != 1:
        raise ValueError("Unknown V8 SDK manifest schema")
    if abi != manifest["abi"]:
        raise ValueError(f"Prebuilt V8 requires ABI {manifest['abi']}, not {abi}")
    if api < manifest["min_api"]:
        raise ValueError(f"Prebuilt V8 requires API {manifest['min_api']} or newer (native ELF TLS)")
    if sdk.is_symlink():
        raise ValueError(f"SDK symlink is not allowed: {sdk}")
    for section, calculate in (("files", file_digest), ("trees", tree_digest)):
        for name, expected in manifest[section].items():
            if calculate(sdk_path(sdk, name)) != expected:
                raise ValueError(f"Prebuilt SDK checksum mismatch: {name}")
    return {key: manifest[key] for key in ("version", "abi", "min_api")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--abi", required=True)
    parser.add_argument("--api", type=int, required=True)
    arguments = parser.parse_args()
    try:
        result = verify_sdk(arguments.sdk, json.loads(MANIFEST.read_text()),
                            arguments.abi, arguments.api)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Invalid Android V8 prebuilt: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())

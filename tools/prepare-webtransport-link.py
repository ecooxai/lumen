#!/usr/bin/env python3
"""Prepare content-stable native WebTransport configuration from pinned evidence.

The target library builder supplies Rust's actual native-static-libs arguments.
This helper validates the installed archive/header and never guesses frameworks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

TARGETS = {
    'macos': 'aarch64-apple-darwin',
    'sim': 'aarch64-apple-ios-sim',
    'ios': 'aarch64-apple-ios',
}


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(data)
    return checksum.hexdigest()


def write_changed(path: Path, text: str) -> None:
    if path.exists() and path.read_text() == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(text)
    temporary.replace(path)


def response_argument(argument: str) -> str:
    if (not isinstance(argument, str) or not argument or len(argument) > 4096
            or any(c in argument for c in '\x00\r\n')):
        raise ValueError('Native linker arguments must be bounded individual strings')
    return '"' + argument.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True, type=Path)
    parser.add_argument('--header', required=True, type=Path)
    parser.add_argument('--response', required=True, type=Path)
    parser.add_argument('--platform', required=True)
    parser.add_argument('--enabled', required=True, choices=['0', '1'])
    args = parser.parse_args()
    header = '#define LUMEN_WEBTRANSPORT_NATIVE ' + args.enabled + '\n'
    response = ''
    if args.enabled == '1':
        prefix = args.prefix.resolve()
        manifest_path = prefix / 'manifest.json'
        manifest = json.loads(manifest_path.read_text())
        if args.platform not in TARGETS or manifest.get('target') != TARGETS[args.platform]:
            raise ValueError('The installed WebTransport library has a different target')
        if manifest.get('backend') != 'wtransport-0.7.2' or manifest.get('version') != '0.7.2':
            raise ValueError('The installed WebTransport library is not the pinned backend')
        archive = prefix / 'lib/liblumen_webtransport_native.a'
        public_header = prefix / 'include/lumen_wt.h'
        if digest(archive) != manifest.get('archiveSHA256'):
            raise ValueError('Installed WebTransport archive hash does not match its manifest')
        if digest(public_header) != manifest.get('headerSHA256'):
            raise ValueError('Installed WebTransport header hash does not match its manifest')
        native_args = manifest.get('nativeLinkArgs')
        if not isinstance(native_args, list) or not 1 <= len(native_args) <= 128:
            raise ValueError('Measured native linker arguments are required')
        response = '\n'.join(response_argument(arg) for arg in native_args) + '\n'
        # Keep the generated header stable, but force a bridge rebuild when the
        # selected prefix or installed provenance changes. Only a digest is
        # embedded; arbitrary path text cannot become C preprocessor input.
        identity = hashlib.sha256((str(prefix) + '\n' + digest(manifest_path)).encode()).hexdigest()
        header += '/* Installed backend identity: ' + identity + ' */\n'
    write_changed(args.response, response)
    write_changed(args.header, header)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, TypeError) as failure:
        raise SystemExit(str(failure))

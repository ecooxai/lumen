#!/usr/bin/env python3
"""Prepare isolated V8/Rust archives for the Apple C++ link.

Run after building v8_monolith. GN's obj/v8_monolith.ninja declares its complete
Rust archive closure in the rlibs binding. Copies under V8OUT/lumen-link rename
only Rust's personality symbol, keeping V8's runtime separate from wgpu's.
The original archives are never modified. --output is a clang response file.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile


RENAME = '_rust_eh_personality=_lumen_v8_rust_eh_personality'
TOOL_PATH = 'third_party/llvm-build/Release+Asserts/bin/llvm-objcopy'
SCHEMA = 1
ASSIGNMENT = re.compile(r'^([A-Za-z0-9_.-]+)\s*=\s*(.*)$')
VARIABLE = re.compile(r'[A-Za-z0-9_-]+')


class PreparationError(Exception):
    pass


def logical_lines(text):
    """Join Ninja $-newlines, including the following indentation, not $$-EOL."""
    pending = ''
    joining = False
    for physical in text.splitlines(keepends=True):
        line = physical.rstrip('\r\n')
        if joining:
            line = line.lstrip(' \t')
        dollars = len(line) - len(line.rstrip('$'))
        continued = physical.endswith('\n') and dollars % 2 == 1
        pending += line[:-1] if continued else line
        joining = continued
        if not continued:
            yield pending
            pending = ''
    if joining:
        raise PreparationError('unterminated Ninja line continuation')


def ninja_parts(value, variables, stack=()):
    """Yield (text, literal) pieces; an escaped/expanded space is part of a path."""
    i = 0
    while i < len(value):
        if value[i] != '$':
            yield value[i], False
            i += 1
            continue
        i += 1
        if i == len(value):
            raise PreparationError('dangling $ in Ninja value')
        if value[i] in '$ :':
            yield value[i], True
            i += 1
            continue
        if value[i] == '{':
            end = value.find('}', i + 1)
            if end < 0:
                raise PreparationError('unterminated Ninja variable reference')
            name = value[i + 1:end]
            i = end + 1
        else:
            match = VARIABLE.match(value, i)
            if not match:
                raise PreparationError(f'unsupported Ninja escape ${value[i]}')
            name = match[0]
            i = match.end()
        if name not in variables:
            raise PreparationError(f'unknown Ninja variable: {name}')
        if name in stack:
            raise PreparationError(f'cyclic Ninja variable: {name}')
        expanded = ''.join(piece for piece, _ in ninja_parts(variables[name], variables, stack + (name,)))
        yield expanded, True


def ninja_paths(value, variables):
    paths, current = [], []
    for piece, literal in ninja_parts(value, variables):
        if not literal and piece in ' \t':
            if current:
                paths.append(''.join(current))
                current = []
        else:
            current.append(piece)
    if current:
        paths.append(''.join(current))
    return paths


def build_parts(line):
    """Find the build separator, skipping escaped colons and variable contents."""
    i = len('build ')
    start = i
    while i < len(line):
        if line[i] == '$':
            if i + 1 < len(line) and line[i + 1] == '{':
                end = line.find('}', i + 2)
                if end < 0:
                    raise PreparationError('unterminated Ninja build variable')
                i = end + 1
            else:
                i += 2
        elif line[i] == ':':
            return line[start:i], line[i + 1:].lstrip()
        else:
            i += 1
    raise PreparationError('Ninja build declaration has no rule separator')


def archive_paths(metadata, v8_out):
    """Read GN's local bindings and the monolith edge; reject an incomplete list.

    The rlibs binding is a list of Ninja-escaped paths. A variable inside one
    path expands as a single path component, following Ninja path-list rules.
    Parent-file variables are deliberately not guessed or treated as empty.
    """
    monolith = (v8_out / 'obj/libv8_monolith.a').resolve()
    file_vars, target_vars = {}, None
    selected = False
    target_inputs = ''
    for line in logical_lines(metadata.read_text()):
        stripped = line.lstrip(' \t')
        if not stripped or stripped.startswith('#'):
            continue
        assignment = ASSIGNMENT.match(stripped)
        if line[0] not in ' \t':
            selected = False
            if assignment:
                file_vars[assignment[1]] = assignment[2]
            elif line.startswith('build '):
                outputs, tail = build_parts(line)
                names = ninja_paths(outputs, file_vars)
                if any((v8_out / name).resolve() == monolith for name in names if name != '|'):
                    if target_vars is not None:
                        raise PreparationError('multiple monolith build edges in Ninja metadata')
                    rule_and_inputs = re.split(r'[ \t]+', tail, maxsplit=1)
                    rule = rule_and_inputs[0]
                    target_inputs = rule_and_inputs[1] if len(rule_and_inputs) == 2 else ''
                    if not rule or rule == 'phony':
                        raise PreparationError('monolith metadata does not declare an archive build')
                    target_vars = dict(file_vars)
                    selected = True
        elif selected:
            if not assignment:
                raise PreparationError(f'invalid monolith binding: {stripped[:100]}')
            target_vars[assignment[1]] = assignment[2]
    if target_vars is None:
        raise PreparationError('Ninja metadata has no build edge for obj/libv8_monolith.a')
    inputs = ninja_paths(target_inputs, target_vars)
    libraries = ninja_paths(target_vars.get('rlibs', ''), target_vars)
    if any(name.endswith('.rlib') for name in inputs) and not libraries:
        raise PreparationError('monolith has Rust inputs but no populated rlibs binding')
    paths = [monolith]
    for name in libraries:
        if not name.endswith('.rlib'):
            raise PreparationError(f'non-rlib entry in Ninja rlibs binding: {name}')
        if any(char in name for char in '\x00\r\n'):
            raise PreparationError('archive path contains an unsupported control character')
        paths.append((v8_out / name).resolve())
    declared = set(paths[1:])
    for name in inputs:
        if name.endswith('.rlib') and (v8_out / name).resolve() not in declared:
            raise PreparationError(f'Rust input missing from rlibs binding: {name}')
    return paths


def fingerprint(path):
    info = path.stat()
    if not stat.S_ISREG(info.st_mode):
        raise PreparationError(f'not a regular file: {path}')
    return {'size': info.st_size, 'mtime_ns': info.st_mtime_ns,
            'ctime_ns': info.st_ctime_ns, 'device': info.st_dev, 'inode': info.st_ino}


def require_archive(path):
    try:
        with path.open('rb') as stream:
            magic = stream.read(8)
    except OSError as error:
        raise PreparationError(f'cannot read required archive {path}: {error}') from error
    if magic != b'!<arch>\n':
        raise PreparationError(f'not a regular ar archive (thin archives unsupported): {path}')


def atomic_write(path, contents):
    encoded = contents.encode()
    if path.is_file() and path.read_bytes() == encoded:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(encoded)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def response_path(path):
    text = str(path)
    if any(char in text for char in '\x00\r\n'):
        raise PreparationError('response file path contains an unsupported control character')
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def prepare(v8_dir, v8_out, output, jobs=3):
    if jobs < 1:
        raise PreparationError('--jobs must be at least 1')
    v8_dir, v8_out, output = (Path(path).expanduser().resolve() for path in (v8_dir, v8_out, output))
    metadata = v8_out / 'obj/v8_monolith.ninja'
    tool = (v8_dir / TOOL_PATH).resolve()
    if not tool.is_file() or not os.access(tool, os.X_OK):
        raise PreparationError(f'V8 bundled llvm-objcopy is missing or not executable: {tool}')
    metadata_state = fingerprint(metadata)
    paths = archive_paths(metadata, v8_out)
    for path in paths:
        require_archive(path)
    link_dir = v8_out / 'lumen-link'
    manifest_path = link_dir / 'manifest.json'
    lock_path = link_dir / '.prepare.lock'
    if output in set(paths) | {metadata, tool, manifest_path, lock_path}:
        raise PreparationError('--output would overwrite a dependency or preparation metadata')
    link_dir.mkdir(parents=True, exist_ok=True)
    with lock_path.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        tool_state = {'path': str(tool), **fingerprint(tool)}
        transform = {'schema': SCHEMA, 'rename': RENAME, 'tool': tool_state,
                     'helper_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        try:
            previous = json.loads(manifest_path.read_text())
        except (FileNotFoundError, ValueError):
            previous = {}
        if not isinstance(previous, dict):
            previous = {}
        old_entries = previous.get('archives', {})
        if not isinstance(old_entries, dict) or previous.get('transform') != transform:
            old_entries = {}
        states = {str(path): fingerprint(path) for path in paths}
        records, pending = {}, []
        for path in dict.fromkeys(paths):
            source = str(path)
            key = hashlib.sha256(json.dumps([source, states[source], transform], sort_keys=True).encode()).hexdigest()
            destination = link_dir / 'archives' / f'{path.stem[:72]}-{key}{path.suffix}'
            if destination.resolve() in set(paths) | {output}:
                raise PreparationError(f'generated archive would overwrite an input: {destination}')
            record = {'input': states[source], 'output': str(destination)}
            old = old_entries.get(source, {})
            hit = False
            if isinstance(old, dict) and old.get('input') == states[source] and old.get('output') == str(destination):
                try:
                    hit = old.get('output_stat') == fingerprint(destination)
                    if hit:
                        require_archive(destination)
                except (OSError, PreparationError):
                    hit = False
            if hit:
                records[source] = old
            else:
                pending.append((path, destination, record))

        def transform_one(item):
            source, destination, record = item
            destination.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temporary = tempfile.mkstemp(prefix=destination.name + '.', dir=destination.parent)
            os.close(descriptor)
            try:
                result = subprocess.run([str(tool), '--redefine-sym=' + RENAME, str(source), temporary],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors='replace')
                if result.returncode:
                    detail = (result.stderr or result.stdout)[-4000:].strip()
                    raise PreparationError(f'llvm-objcopy failed for {source} (exit {result.returncode}): {detail}')
                require_archive(Path(temporary))
                if fingerprint(source) != states[str(source)]:
                    raise PreparationError(f'archive changed during preparation; finish the V8 build and retry: {source}')
                os.replace(temporary, destination)
                record['output_stat'] = fingerprint(destination)
                return str(source), record
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)

        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for source, record in pool.map(transform_one, pending):
                records[source] = record
        if tool_state != {'path': str(tool), **fingerprint(tool)}:
            raise PreparationError('llvm-objcopy changed during preparation; retry with a stable toolchain')
        if any(states[str(path)] != fingerprint(path) for path in paths):
            raise PreparationError('V8 archives changed during preparation; finish the V8 build and retry')
        if metadata_state != fingerprint(metadata):
            raise PreparationError('V8 Ninja metadata changed during preparation; regenerate and retry')
        # Content-addressed outputs leave the last published response file valid
        # even if a later transformation fails. No original input is edited.
        response = ''.join(response_path(Path(records[str(path)]['output'])) + '\n' for path in paths)
        manifest = {'transform': transform, 'metadata': str(metadata), 'archives': records}
        atomic_write(manifest_path, json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        atomic_write(output, response)
        return {'archives': len(paths), 'transformed': len(pending), 'output': str(output)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--v8-dir', required=True, type=Path)
    parser.add_argument('--v8-out', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    try:
        result = prepare(args.v8_dir, args.v8_out, args.output, args.jobs)
    except (OSError, PreparationError) as error:
        parser.exit(1, f'prepare-v8-link: {error}\n')
    print(f'prepare-v8-link: {result["archives"]} archives, {result["transformed"]} transformed; {result["output"]}')


if __name__ == '__main__':
    main()

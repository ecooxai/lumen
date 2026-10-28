#!/usr/bin/env python3
"""Read the arm64 macOS declarations of a local development dependency closure.

This describes link-time minimums. It is not an older macOS runtime test and
does not make the development bundle self-contained.
"""
from pathlib import Path
import hashlib
import re
import subprocess
import time


def sha256(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def version_tuple(value):
    if not isinstance(value, str) or not re.fullmatch(r'[1-9]\d*(?:\.\d+){0,2}', value):
        raise ValueError('Invalid macOS minimum version')
    numbers = tuple(int(item) for item in value.split('.'))
    return numbers + (0,) * (3 - len(numbers))


def version_text(value):
    numbers = version_tuple(value)
    return '.'.join(map(str, numbers if numbers[2] else numbers[:2]))


def parse_commands(text):
    commands, current = [], None
    for line in text.splitlines():
        match = re.fullmatch(r'\s*cmd (LC_[A-Z0-9_]+)\s*', line)
        if match:
            if current is not None:
                commands.append(current)
            current = {'command': match.group(1)}
        elif current is not None:
            match = re.fullmatch(r'\s*(platform|minos|sdk|version)\s+(\S+)\s*', line)
            if match:
                current[match.group(1)] = match.group(2)
            match = re.fullmatch(r'\s*(name|path) (.+) \(offset \d+\)\s*', line)
            if match:
                current[match.group(1)] = match.group(2)
    if current is not None:
        commands.append(current)
    for command in commands:
        if command['command'] in ('LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                                   'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB') and not command.get('name'):
            raise ValueError('A dylib dependency load command has no parsed install name')
        if command['command'] == 'LC_RPATH' and not command.get('path'):
            raise ValueError('A runtime search-path command has no parsed path')
    versions = []
    for command in commands:
        if command['command'] == 'LC_BUILD_VERSION':
            if command.get('platform') not in ('1', 'MACOS', 'macOS'):
                raise ValueError('A dependency is not a macOS image')
            versions.append(command.get('minos'))
        elif command['command'] == 'LC_VERSION_MIN_MACOSX':
            versions.append(command.get('version'))
        elif command['command'].startswith('LC_VERSION_MIN_'):
            raise ValueError('A dependency declares a different Apple platform')
    if len(versions) != 1:
        raise ValueError('Exactly one arm64 macOS minimum declaration is required')
    minimum = version_text(versions[0])
    dependencies = [item['name'] for item in commands if item['command'] in
        ('LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
         'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB') and 'name' in item]
    rpaths = [item['path'] for item in commands if item['command'] == 'LC_RPATH' and 'path' in item]
    return minimum, dependencies, rpaths


def inspect_closure(project, executable, *, maximum_seconds=45, maximum_files=128):
    project, executable = Path(project).resolve(), Path(executable).resolve()
    if not executable.is_file() or not executable.is_relative_to(project):
        raise ValueError('The development executable must be beneath the project')
    started = time.monotonic()
    queue, files, seen, system = [(executable, [])], [], set(), set()
    allowed = (project, Path('/opt/homebrew').resolve())
    while queue:
        if time.monotonic() - started > maximum_seconds or len(seen) >= maximum_files:
            raise RuntimeError('The bounded dependency closure inspection did not finish')
        path, inherited_rpaths = queue.pop(0)
        if path in seen:
            continue
        seen.add(path)
        if not path.is_file() or not any(path.is_relative_to(base) for base in allowed):
            raise ValueError('A dependency is outside the project and installed Homebrew roots')
        result = subprocess.run(['/usr/bin/otool', '-arch', 'arm64', '-l', str(path)],
            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=5, check=False)
        if result.returncode != 0 or len(result.stdout) > 2 * 1024 * 1024:
            raise RuntimeError('The arm64 load commands could not be inspected: ' + path.name)
        minimum, dependencies, local_rpaths = parse_commands(result.stdout)
        def expand(value):
            return value.replace('@loader_path', str(path.parent)).replace('@executable_path', str(executable.parent))
        rpaths = [expand(value) for value in local_rpaths] + inherited_rpaths
        edges = []
        for name in dependencies:
            if name.startswith(('/System/Library/', '/usr/lib/')):
                system.add(name)
                edges.append({'installName': name, 'systemSharedCache': True})
                continue
            if name.startswith('@rpath/'):
                candidates = [Path(value) / name[len('@rpath/'):] for value in rpaths]
            elif name.startswith(('@loader_path/', '@executable_path/', '/')):
                candidates = [Path(expand(name))]
            else:
                raise ValueError('An unqualified dependency install name cannot be resolved')
            resolved = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if resolved is None:
                raise ValueError('A non-system dependency could not be resolved: ' + name)
            if str(resolved).startswith(('/System/Library/', '/usr/lib/')):
                system.add(name)
                edges.append({'installName': name, 'systemSharedCache': True})
                continue
            if not any(resolved.is_relative_to(base) for base in allowed):
                raise ValueError('A non-system dependency escaped the reviewed roots')
            edges.append({'installName': name, 'resolved': str(resolved)})
            queue.append((resolved, rpaths))
        files.append({'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha256(path),
                      'minimumMacOSVersion': minimum, 'dependencies': edges})
    if not files:
        raise RuntimeError('No Mach-O image was inspected')
    maximum = max((entry['minimumMacOSVersion'] for entry in files), key=version_tuple)
    return {'schemaVersion': 1, 'architecture': 'arm64', 'platform': 'macos',
            'executable': str(executable), 'maximumDeclaredMinimum': maximum,
            'nonSystemClosureFullyResolved': True, 'files': files,
            'systemSharedCacheInstallNames': sorted(system),
            'olderMacOSRuntimeTested': False, 'selfContainedDistributionClaimed': False,
            'elapsedSeconds': round(time.monotonic() - started, 3)}

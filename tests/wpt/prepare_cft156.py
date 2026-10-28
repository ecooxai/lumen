#!/usr/bin/env python3
"""Fetch the exact official Mac arm64 Chrome for Testing reference into output.

Authoring only until run on the authorized Mac. Does not launch a browser,
modify /Applications, import a profile, change security settings, or run an
updater. The official catalog and observed object metadata are pinned below.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import plistlib
import shutil
import stat
import subprocess
import sys
import time
import unicodedata
import urllib.error
import urllib.request
import zipfile

VERSION = '156.0.8078.47'
REVISION = '1706539'
URL = ('https://storage.googleapis.com/chrome-for-testing-public/' + VERSION +
       '/mac-arm64/chrome-mac-arm64.zip')
GENERATION = '1792453477297060'
LENGTH = 194932023
MD5 = '306e5acd340b643b3bdceda0acb3fdb7'
PREFIX = 'chrome-mac-arm64'
CHUNK = 1024 * 1024
MAX_EXPANDED = 3 * 1024**3


def sha256(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def save(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
    os.replace(temporary, path)


def hash_fields(headers):
    if hasattr(headers, 'get_all'):
        return headers.get_all('x-goog-hash') or []
    value = headers.get('x-goog-hash')
    return [value] if value is not None else []


def verify_headers(headers):
    if headers.get('Content-Length') != str(LENGTH) or headers.get('x-goog-generation') != GENERATION:
        raise RuntimeError('Official object size or generation changed')
    if headers.get('Content-Encoding', 'identity') not in ('', 'identity'):
        raise RuntimeError('Unexpected content encoding')
    observed = []
    for field in hash_fields(headers):
        for entry in field.split(','):
            algorithm, separator, value = entry.strip().partition('=')
            if algorithm.strip().lower() == 'md5':
                if not separator:
                    raise RuntimeError('Malformed official object MD5 header')
                observed.append(base64.b64decode(value.strip(), validate=True).hex())
    if not observed or any(value != MD5 for value in observed):
        raise RuntimeError('Official object MD5 changed')


class RejectRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        raise RuntimeError('Unexpected archive redirect: ' + newurl)


def download(path, record):
    request = urllib.request.Request(URL, headers={'Accept-Encoding': 'identity', 'If-Match': '"' + MD5 + '"'})
    opener = urllib.request.build_opener(RejectRedirect())
    deadline = time.monotonic() + 300
    count, md5, sha = 0, hashlib.md5(), hashlib.sha256()
    with opener.open(request, timeout=20) as response:
        if response.status != 200 or response.geturl() != URL:
            raise RuntimeError('Unexpected archive response')
        verify_headers(response.headers)
        record['headers'] = dict(response.headers.items())
        record['hash_headers'] = hash_fields(response.headers)
        with path.open('xb') as output:
            while True:
                if time.monotonic() > deadline:
                    raise TimeoutError('Download exceeded its 300-second budget')
                data = response.read(CHUNK)
                if not data:
                    break
                count += len(data)
                if count > LENGTH:
                    raise RuntimeError('Archive exceeded the pinned size')
                md5.update(data); sha.update(data); output.write(data)
            output.flush(); os.fsync(output.fileno())
    if count != LENGTH or md5.hexdigest() != MD5:
        raise RuntimeError('Archive content failed pinned length/MD5 verification')
    record.update(bytes=count, md5=md5.hexdigest(), sha256=sha.hexdigest())


def archive_plan(path):
    """Check paths conservatively for case/Unicode-insensitive Mac volumes."""
    def canonical(value):
        return unicodedata.normalize('NFD', value.casefold())

    names, links, expanded = set(), {}, 0
    with zipfile.ZipFile(path) as archive:
        members = archive.infolist()
        if not members or len(members) > 20000:
            raise RuntimeError('Unexpected archive member count')
        for member in members:
            raw = member.orig_filename
            name = raw.rstrip('/')
            components = PurePosixPath(name).parts
            normalized = canonical(name)
            if ('\x00' in raw or '\\' in raw or '//' in raw or name.startswith('/') or
                    not components or components[0] != PREFIX or
                    any(part in ('..', '.') for part in raw.split('/')) or normalized in names):
                raise RuntimeError('Unsafe or duplicate archive member: ' + repr(raw))
            names.add(normalized)
            expanded += member.file_size
            if expanded > MAX_EXPANDED or member.file_size > 512 * 1024**2:
                raise RuntimeError('Archive expanded-size budget exceeded')
            mode = member.external_attr >> 16
            kind = stat.S_IFMT(mode)
            if kind not in (0, stat.S_IFREG, stat.S_IFDIR, stat.S_IFLNK):
                raise RuntimeError('Unexpected archive member type')
            if kind == stat.S_IFLNK:
                if member.file_size > 4096:
                    raise RuntimeError('Oversized archive symlink')
                target = archive.read(member).decode('utf-8')
                if not target or target.startswith('/') or '\x00' in target or '\\' in target:
                    raise RuntimeError('Unsafe archive symlink target')
                links[normalized] = target

    def resolve(name):
        queue, resolved, followed = name.split('/'), [], 0
        while queue:
            part = queue.pop(0)
            if part in ('', '.'):
                continue
            if part == '..':
                if len(resolved) <= 1:
                    raise RuntimeError('Archive symlink escapes its root')
                resolved.pop()
                continue
            resolved.append(canonical(part))
            current = '/'.join(resolved)
            if current in links:
                followed += 1
                if followed > 64:
                    raise RuntimeError('Archive symlink cycle or excessive depth')
                resolved.pop()
                queue = links[current].split('/') + queue
        if not resolved or resolved[0] != PREFIX:
            raise RuntimeError('Archive member escapes its root')
        return '/'.join(resolved)

    for name in names:
        resolve(name)
    return {'member_count': len(names), 'symlink_count': len(links), 'expanded_bytes': expanded}


def checked(command, timeout):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, errors='replace', timeout=timeout)
    evidence = {'argv': command, 'returncode': result.returncode,
                'stdout': result.stdout[-12000:], 'stderr': result.stderr[-12000:]}
    if result.returncode:
        raise RuntimeError('Verification command failed: ' + json.dumps(evidence))
    return evidence


def prepare(repo, output, catalog, attempts):
    if sys.platform != 'darwin' or platform.machine() != 'arm64':
        raise RuntimeError('Run only on the authorized arm64 Mac')
    repo, output, catalog = repo.resolve(), output.absolute(), catalog.resolve(strict=True)
    allowed = repo / 'output/wpt/reference-browsers'
    output = output.resolve()
    if not output.is_relative_to(allowed) or output == allowed:
        raise RuntimeError('Fresh output must be below project output/wpt/reference-browsers')
    if output.exists():
        raise RuntimeError('Output already exists; no reference installation is overwritten')
    manifest = json.loads(catalog.read_text())
    stable = manifest['channels']['Stable']
    if stable['version'] != VERSION or stable['revision'] != REVISION:
        raise RuntimeError('Saved official catalog no longer identifies this exact stable reference')
    matches = [item for item in stable['downloads']['chrome'] if item['platform'] == 'mac-arm64']
    if len(matches) != 1 or matches[0]['url'] != URL:
        raise RuntimeError('Saved official catalog download differs')
    if shutil.disk_usage(repo).free < LENGTH + 3 * 1024**3:
        raise RuntimeError('Insufficient free space to stage the reference browser')
    output.mkdir(parents=True, exist_ok=False)
    summary = output / 'reference.json'
    record = {'schema': 1, 'status': 'started', 'version': VERSION, 'revision': REVISION,
              'url': URL, 'generation': GENERATION, 'pinned_md5': MD5, 'pinned_bytes': LENGTH,
              'helper_sha256': sha256(__file__), 'catalog_sha256': sha256(catalog),
              'scope': 'Project-local full Chrome for Testing; no browser launch or user-profile access',
              'attempts': []}
    archive, extracted = output / 'chrome-mac-arm64.zip', output / 'extracted'
    try:
        save(output / 'official-catalog.json', manifest)
        save(summary, record)
        for number in range(1, attempts + 1):
            if number > 1:
                time.sleep(30)
            attempt = {'number': number, 'started_unix_ms': round(time.time() * 1000)}
            record['attempts'].append(attempt)
            try:
                download(archive, attempt)
                attempt['status'] = 'verified'
                save(summary, record)
                break
            except urllib.error.HTTPError as error:
                attempt.update(status='http_error', code=error.code, error=str(error))
                save(summary, record)
                if error.code != 503 or number == attempts:
                    raise
                if archive.exists():
                    archive.unlink()
        record['archive'] = archive_plan(archive)
        if shutil.disk_usage(repo).free < record['archive']['expanded_bytes'] + 3 * 1024**3:
            raise RuntimeError('Insufficient free space for validated extraction')
        extracted.mkdir(exist_ok=False)
        record['extract'] = checked(['/usr/bin/ditto', '-x', '-k', str(archive), str(extracted)], 180)
        app = extracted / PREFIX / 'Google Chrome for Testing.app'
        info_path = app / 'Contents/Info.plist'
        info = plistlib.loads(info_path.read_bytes())
        if info.get('CFBundleShortVersionString') != VERSION:
            raise RuntimeError('Extracted app version differs from its official catalog')
        executable = info.get('CFBundleExecutable')
        if not isinstance(executable, str) or '/' in executable or executable in ('', '.', '..'):
            raise RuntimeError('Unexpected bundle executable name')
        binary = app / 'Contents/MacOS' / executable
        if not binary.is_file() or not binary.resolve().is_relative_to(app.resolve()):
            raise RuntimeError('Bundle executable is missing or escaped its bundle')
        record['signature_verify'] = checked(['/usr/bin/codesign', '--verify', '--strict', '--deep', str(app)], 120)
        record['signature_identity'] = checked(['/usr/bin/codesign', '-dv', '--verbose=4', str(app)], 30)
        record['architectures'] = checked(['/usr/bin/lipo', '-archs', str(binary)], 30)
        if 'arm64' not in record['architectures']['stdout'].split():
            raise RuntimeError('Reference executable lacks arm64')
        record.update(app=str(app), binary=str(binary), binary_sha256=sha256(binary),
                      info_sha256=sha256(info_path), bundle_identifier=info.get('CFBundleIdentifier'),
                      status='verified', browser_launched=False)
    except Exception as error:
        record.update(status='failed', error=type(error).__name__ + ': ' + str(error))
        raise
    finally:
        save(summary, record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--attempts', type=int, default=20)
    args = parser.parse_args()
    if not 1 <= args.attempts <= 20:
        parser.error('--attempts must be between 1 and 20')
    result = prepare(args.repo, args.output, args.catalog, args.attempts)
    print(json.dumps({key: result[key] for key in ('status', 'version', 'binary', 'binary_sha256', 'browser_launched')}))


if __name__ == '__main__':
    main()

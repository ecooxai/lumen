#!/usr/bin/env python3
"""Stage a hash-pinned official ICU79 bottle locally, then probe native IDNA.

This never installs a Homebrew keg, changes host symlinks, or touches V8. Run
through tools/monitor_process.py. A successful probe is not a browser WPT result.
Bottle hashes are from Homebrew/homebrew-core commit
8701235251a995187352bd621a2f182a1127fa8c, Formula/i/icu4c@79.rb.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import time


ROOT = Path(__file__).resolve().parents[2]
BOTTLES = {
    'arm64_sequoia': '7d05da26db392a008abe89706adcf8b089e08d3431535ac95a5c984ceda88153',
    'arm64_tahoe': '4424c58d257d07b28086814a575baa9e462560460fdea5455581d99062896cec',
    'arm64_golden_gate': '8499235357ff52424e3fcc647a6ce57050af9f10904554df90319e6a088f665d',
}


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('Run on the actual Apple Silicon Mac')
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'out'):
        raise RuntimeError('Use a new task output inside this project/out')
    out.mkdir(parents=True, exist_ok=False)
    began = time.monotonic()
    record = {'kind': 'isolated-prebuilt-icu79-idna-diagnostic', 'officialWPT': False,
        'startedUTC': datetime.now(timezone.utc).isoformat(), 'stages': [], 'passed': False,
        'scriptSHA256': sha(__file__), 'homebrewFormulaCommit': '8701235251a995187352bd621a2f182a1127fa8c',
        'officialFormula': 'https://github.com/Homebrew/homebrew-core/blob/8701235251a995187352bd621a2f182a1127fa8c/Formula/i/icu4c%4079.rb',
        'officialRelease': 'https://github.com/unicode-org/icu/releases/tag/release-79.1',
        'scope': 'Only project-local files created; no keg installation, host symlink change, V8 rebuild, or browser WPT run.'}

    def save():
        payload = json.dumps(record, indent=2, ensure_ascii=True) + '\n'
        temp = out / 'receipt.json.tmp'
        with temp.open('w') as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        temp.replace(out / 'receipt.json')
        if (out / 'receipt.json').read_text() != payload:
            raise RuntimeError('Receipt readback differs')

    def run(label, command, limit=30, env=None):
        started = time.monotonic()
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=limit, cwd=ROOT, env=env)
        log = out / (label + '.log')
        log.write_text(process.stdout)
        record['stages'].append({'name': label, 'command': command, 'exitCode': process.returncode,
            'durationSeconds': round(time.monotonic() - started, 6), 'logSHA256': sha(log)})
        save()
        if process.returncode:
            raise RuntimeError(label + ' failed; inspect its preserved log')
        return process.stdout

    old = Path('/opt/homebrew/opt/icu4c')
    original_link = str(old.resolve(strict=True))
    try:
        version = run('macos-version', ['sw_vers', '-productVersion']).strip()
        tag = {15: 'arm64_sequoia', 26: 'arm64_tahoe', 27: 'arm64_golden_gate'}.get(int(version.split('.')[0]))
        if tag is None:
            raise RuntimeError('No inspected compatible bottle for this macOS version: ' + version)
        record.update(macosVersion=version, bottleTag=tag, expectedBottleSHA256=BOTTLES[tag],
            originalHostICUPrefix=original_link)
        environment = os.environ.copy()
        environment.update(HOMEBREW_NO_AUTO_UPDATE='1', HOMEBREW_NO_INSTALL_CLEANUP='1',
            HOMEBREW_NO_ENV_HINTS='1', HOMEBREW_CACHE=str(out / 'brew-cache'))
        run('fetch-official-bottle', ['/opt/homebrew/bin/brew', 'fetch', '--force-bottle',
            '--bottle-tag=' + tag, 'icu4c@79'], limit=150, env=environment)
        candidates = [path for path in (out / 'brew-cache').rglob('*.tar.gz') if path.is_file()]
        matches = [path for path in candidates if sha(path) == BOTTLES[tag]]
        if len(matches) != 1:
            raise RuntimeError('Fetched bottle does not match the independently pinned official formula hash')
        archive = matches[0]
        record['bottleRelativePath'] = str(archive.relative_to(ROOT))
        extract = out / 'bottle'
        extract.mkdir()
        with tarfile.open(archive, 'r:*') as source:
            members = source.getmembers()
            for member in members:
                path = Path(member.name)
                if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != 'icu4c@79':
                    raise RuntimeError('Unexpected bottle archive member path')
                if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                    raise RuntimeError('Unexpected special archive member')
            source.extractall(extract, members=members, filter='data')
        prefix = extract / 'icu4c@79/79.1'
        if not (prefix / 'include/unicode/uidna.h').is_file():
            raise RuntimeError('Expected ICU79 development headers absent')
        libraries = sorted(path for path in (prefix / 'lib').glob('*.dylib') if path.is_file() and not path.is_symlink())
        before = {str(path.relative_to(prefix)): sha(path) for path in libraries}
        relocations = []
        for index, library in enumerate(libraries):
            linked = run('dependencies-' + str(index), ['otool', '-L', str(library)])
            changes = []
            for line in linked.splitlines()[1:]:
                dependency = line.strip().split(' (compatibility version', 1)[0]
                basename = Path(dependency).name
                if basename.startswith('libicu') and basename.endswith('.dylib'):
                    if not (prefix / 'lib' / basename).is_file():
                        raise RuntimeError('Bottle ICU dependency is absent: ' + basename)
                    changes.extend(['-change', dependency, '@loader_path/' + basename])
            if changes:
                run('relocate-' + str(index), ['install_name_tool', *changes, '-id', str(library), str(library)])
            else:
                run('identity-' + str(index), ['install_name_tool', '-id', str(library), str(library)])
            run('codesign-' + str(index), ['codesign', '--force', '--sign', '-', str(library)])
            run('verify-signature-' + str(index), ['codesign', '--verify', '--strict', str(library)])
            relocations.append({'library': str(library.relative_to(prefix)), 'changes': changes,
                'originalSHA256': before[str(library.relative_to(prefix))], 'relocatedSHA256': sha(library)})
        record.update(prefix=str(prefix.relative_to(ROOT)), libraries=relocations)
        library = ctypes.CDLL(str(prefix / 'lib/libicuuc.79.dylib'))
        version_array, unicode_array = (ctypes.c_uint8 * 4)(), (ctypes.c_uint8 * 4)()
        library.u_getVersion_79(version_array)
        library.u_getUnicodeVersion_79(unicode_array)
        record.update(ICUVersion=list(version_array), UnicodeDataVersion=list(unicode_array))
        if list(version_array)[:2] != [79, 1] or list(unicode_array) != [18, 0, 0, 0]:
            raise RuntimeError('Staged ICU library or Unicode data version differs from the intended dependency')

        class Info(ctypes.Structure):
            _fields_ = [('size', ctypes.c_int16), ('different', ctypes.c_uint8), ('reservedB', ctypes.c_uint8),
                ('errors', ctypes.c_uint32), ('reservedI2', ctypes.c_int32), ('reservedI3', ctypes.c_int32)]
        if ctypes.sizeof(Info) != 16:
            raise RuntimeError('Unexpected UIDNAInfo ABI')
        open_uts = library.uidna_openUTS46_79
        open_uts.argtypes, open_uts.restype = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_int32)], ctypes.c_void_p
        convert = library.uidna_nameToASCII_UTF8_79
        convert.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32, ctypes.c_void_p,
            ctypes.c_int32, ctypes.POINTER(Info), ctypes.POINTER(ctypes.c_int32)]
        convert.restype = ctypes.c_int32
        close = library.uidna_close_79
        close.argtypes = [ctypes.c_void_p]
        status = ctypes.c_int32(0)
        handle = open_uts(60, ctypes.byref(status))
        if not handle or status.value > 0:
            raise RuntimeError('UIDNA initialization failed')
        failures = ROOT / 'agent/evidence/10.292019r-mac004-final-addendum/url-failures.json'
        record['baselineFailureReceiptSHA256'] = sha(failures)
        baseline = json.loads(failures.read_text())['rows'][0]['failedAssertions']
        if len(baseline) != 7:
            raise RuntimeError('Frozen baseline failure set changed')
        cases = [(item['name'], item['name'][len('ToASCII("'):-2], None, 0) for item in baseline]
        cases += [
            ('stable non-ASCII mapping', 'bücher.example', 'xn--bcher-kva.example', 0),
            ('nontransitional sharp-s', 'faß.de', 'xn--fa-hia.de', 0),
            ('replacement character stays disallowed', '\ufffd.example', None, 0x80),
            ('noncharacter stays disallowed', '\uffff.example', None, 0x80),
            ('leading combining mark stays rejected', '\u0301a.example', None, 0x40),
            ('invalid joiner context stays rejected', 'a\u200db.example', None, 0x1000),
            ('invalid bidirectional label stays rejected', '\u05d0a.example', None, 0x800),
        ]
        rows = []
        try:
            for label, domain, expected, required_error in cases:
                data, destination = domain.encode('utf-8'), ctypes.create_string_buffer(4096)
                info, status = Info(), ctypes.c_int32(0)
                info.size = ctypes.sizeof(Info)
                count = convert(handle, data, len(data), destination, len(destination), ctypes.byref(info), ctypes.byref(status))
                value = destination.raw[:max(0, min(count, len(destination)))].decode('utf-8', 'replace')
                accepted = status.value <= 0 and (info.errors == 0 if required_error == 0 else bool(info.errors & required_error))
                if expected is not None:
                    accepted = accepted and value == expected
                rows.append({'name': label, 'UErrorCode': status.value, 'UIDNAInfoErrors': info.errors,
                    'output': value, 'expectedOutput': expected, 'requiredErrorBit': required_error, 'passed': accepted})
        finally:
            close(handle)
        record.update(sourceUTS46Options=60, probes=rows, probesPassed=sum(row['passed'] for row in rows),
            probesTotal=len(rows), urlSourceSHA256=sha(ROOT / 'src/base/url.c'))
        if not all(row['passed'] for row in rows):
            raise RuntimeError('An unchanged IDNA compatibility or rejection expectation failed')
        if str(old.resolve(strict=True)) != original_link:
            raise RuntimeError('Host ICU alias changed unexpectedly during staging')
        record['hostICUAliasUnchanged'] = True
        record['passed'] = True
    except Exception as error:
        record['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        record.update(finishedUTC=datetime.now(timezone.utc).isoformat(), durationSeconds=round(time.monotonic() - began, 6))
        save()
    print(json.dumps({key: record.get(key) for key in ('passed', 'prefix', 'ICUVersion', 'UnicodeDataVersion',
        'probesPassed', 'probesTotal', 'durationSeconds', 'error')}), flush=True)
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

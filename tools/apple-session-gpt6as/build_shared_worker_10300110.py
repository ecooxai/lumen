"""Build/freeze a native SharedWorker candidate and preserve monitored gates."""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reviewed-source', default='33c44c37f98d99fe9f748ba89be3018f61fb1e5d')
parser.add_argument('--output', type=Path, default=Path('out/10300110-mac/native-shared-001'))
parser.add_argument('--transfer-acceptance', action='store_true')
parser.add_argument('--icu-prefix', type=Path)
parser.add_argument('--icu-receipt', type=Path)
args = parser.parse_args()
root = Path.cwd().resolve()
branch = subprocess.check_output(['git', 'branch', '--show-current'], text=True).strip()
if branch != 'gpt6-astra-pro_builtin-agents_lumen-10.30-0110-sharedworker':
    raise SystemExit('Unexpected task branch; preserve current state')
if sys.platform != 'darwin':
    raise SystemExit('This carrier requires the actual native Mac host')
source_base = subprocess.check_output(['git', 'rev-parse', args.reviewed_source + '^{commit}'], text=True).strip()
subprocess.run(['git', 'diff', '--quiet', source_base, '--', 'src', 'Makefile', 'ios', 'tests/jsrun.c'], check=True)
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
out = (root / args.output).resolve()
out.relative_to(root / 'out')
out.mkdir(parents=True, exist_ok=False)
build = 'build-apple-load-20261029'
make_variables = {'BUILD': build, 'MODE': 'release', 'RTC': '1', 'WT': '1'}
if args.icu_prefix:
    prefix = args.icu_prefix.resolve()
    prefix.relative_to(root / 'out')
    if not (prefix / 'include/unicode/uidna.h').is_file():
        raise SystemExit('The pinned ICU prefix has no UIDNA header')
    make_variables['ICU_PREFIX'] = str(prefix)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


icu_record = None
v8_hashes = {}
if args.icu_prefix:
    if args.icu_receipt is None:
        raise SystemExit('A pinned ICU build requires its successful staging receipt')
    icu_receipt = args.icu_receipt.resolve()
    icu_receipt.relative_to(root / 'out')
    icu_record = json.loads(icu_receipt.read_text())
    if (not icu_record.get('passed') or not icu_record.get('hostICUAliasUnchanged')
            or icu_record.get('expectedBottleSHA256') != '4424c58d257d07b28086814a575baa9e462560460fdea5455581d99062896cec'
            or icu_record.get('ICUVersion') != [79, 1, 0, 0]
            or icu_record.get('UnicodeDataVersion') != [18, 0, 0, 0]
            or icu_record.get('probesPassed') != icu_record.get('probesTotal')
            or icu_record.get('probesTotal') != 14
            or (root / icu_record['prefix']).resolve() != prefix
            or sha(root / 'src/base/url.c') != icu_record['urlSourceSHA256']
            or str(Path('/opt/homebrew/opt/icu4c').resolve()) != icu_record['originalHostICUPrefix']):
        raise SystemExit('The isolated ICU acceptance receipt does not match this build')
    for library in icu_record['libraries']:
        if sha(prefix / library['library']) != library['relocatedSHA256']:
            raise SystemExit('A staged ICU library changed: ' + library['library'])
    for name in ('libv8.dylib', 'libv8_libplatform.dylib', 'libv8_libbase.dylib'):
        path = (Path('/opt/homebrew/opt/v8/lib') / name).resolve()
        v8_hashes[str(path)] = sha(path)


names = subprocess.check_output(['git', 'ls-files', '-z', '--', 'src', 'Makefile', 'ios', 'tests/jsrun.c'], text=True).split('\0')
sources = {name: sha(root / name) for name in names if name and (root / name).is_file()}
(out / 'compiled-source-sha256.json').write_text(json.dumps(sources, indent=2) + '\n')
summary = {
    'startedUTC': utc(), 'sourceBaseCommit': head, 'reviewedProductSource': source_base,
    'branch': branch, 'platform': 'Darwin arm64', 'buildCache': build,
    'sourceManifestSHA256': sha(out / 'compiled-source-sha256.json'),
    'prebuiltV8': 'Existing compatible /opt/homebrew/opt/v8; no V8 source build',
    'rtc': True, 'webtransport': True, 'buildMakeVariables': make_variables,
    'icuPrefix': make_variables.get('ICU_PREFIX'), 'stages': [], 'artifacts': {}, 'passed': False,
    'scope': 'Candidate build and focused regressions; full WPT, resource soak and mobile acceptance remain separate.',
}
if icu_record:
    summary['icuAttestation'] = {'stagingReceipt': str(icu_receipt.relative_to(root)),
        'stagingReceiptSHA256': sha(icu_receipt), 'bottleSHA256': icu_record['expectedBottleSHA256'],
        'ICUVersion': icu_record['ICUVersion'], 'UnicodeDataVersion': icu_record['UnicodeDataVersion'],
        'urlSourceSHA256': icu_record['urlSourceSHA256'], 'libraries': icu_record['libraries'],
        'prebuiltV8FileHashes': v8_hashes}
started = time.monotonic()


def save():
    temporary = out / 'summary.json.tmp'
    temporary.write_text(json.dumps(summary, indent=2) + '\n')
    temporary.replace(out / 'summary.json')


def measured(name, command, limit):
    prefix = out / name
    begun = time.monotonic()
    # Isolate test profiles and app-only overrides while preserving the host's
    # network/authentication/proxy/certificate configuration unchanged.
    environment = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
    environment['DEVELOPER_DIR'] = '/Library/Developer/CommandLineTools'
    with tempfile.TemporaryDirectory(prefix='lumen-native-' + name + '-') as profile:
        environment['LUMEN_PROFILE_DIR'] = profile
        process = subprocess.run([
            sys.executable, 'tools/monitor_process.py', '--duration', str(limit),
            '--settle', '0', '--output', str(prefix.relative_to(root)), '--', *command,
        ], capture_output=True, text=True, timeout=limit + 35, env=environment)
    (out / (name + '-collector.log')).write_text(process.stdout + process.stderr)
    report = json.loads(prefix.with_suffix('.json').read_text())
    log = prefix.with_suffix('.log').read_text(errors='replace')
    passed = (report.get('natural_exit_code') == 0 and not report.get('collector_error')
              and not report.get('ended_by_monitor')
              and not report.get('owned_group_cleanup', {}).get('error'))
    summary['stages'].append({
        'name': name, 'startedUTC': report.get('started_utc'), 'finishedUTC': report.get('finished_utc'),
        'durationSeconds': round(time.monotonic() - begun, 3), 'passed': passed, 'command': command,
        'monitor': str(prefix.with_suffix('.json').relative_to(root)),
        'monitorSHA256': sha(prefix.with_suffix('.json')),
        'log': str(prefix.with_suffix('.log').relative_to(root)), 'logSHA256': sha(prefix.with_suffix('.log')),
        'resultLines': [line for line in log.splitlines() if line.startswith(('RESULTS', 'FAIL', 'SHARED_WORKER_RESULTS'))][-24:],
    })
    save()
    return passed


save()
try:
    targets = ['lumen', 'jsrun', 'test_worker_lifecycle', 'test_worker_async_lifecycle', 'test_worker_security']
    if icu_record:
        targets.extend(['test_url', 'test_url_encoding', 'test_url_dom', 'test_url_idl',
                        'test_url_params', 'test_history_origin', 'test_csp_inherit'])
    if not measured('build', ['make', '-j3', *[key + '=' + value for key, value in make_variables.items()],
                              *[build + '/' + target for target in targets]], 240):
        raise RuntimeError('Native candidate build failed; inspect preserved compiler log')
    for name in targets:
        source = root / build / name
        if source.stat().st_size < 1024 or not os.access(source, os.X_OK) or source.read_bytes()[:4] != b'\xcf\xfa\xed\xfe':
            raise RuntimeError('Native Mach-O artifact invalid: ' + name)
        digest = sha(source)
        target = out / (name + '-' + digest[:16])
        shutil.copy2(source, target)
        target.chmod(0o555)
        if sha(target) != digest:
            raise RuntimeError('Frozen artifact copy changed: ' + name)
        summary['artifacts'][name] = {'path': str(target.relative_to(root)), 'sha256': digest, 'bytes': target.stat().st_size}
    save()
    jsrun = str(root / summary['artifacts']['jsrun']['path'])
    lumen = str(root / summary['artifacts']['lumen']['path'])
    for name in targets[2:]:
        if not measured(name, [str(root / summary['artifacts'][name]['path'])], 110):
            raise RuntimeError('Native regression failed: ' + name)
    for label, driver, options in [
        ('shared-worker', 'tests/test_shared_worker.py', ['--output', str(out / 'shared-worker-receipt.json')]),
        ('message-transfer', 'tests/test_message_transfer.py', []),
        ('worker-headers', 'tests/test_worker_response_headers.py', ['--output', str(out / 'worker-headers-receipt.json')]),
        ('parser-policy', 'tests/test_parser_handler_csp.py', ['--output', str(out / 'parser-policy-receipt.json')]),
        ('data-form-policy', 'tests/test_data_form_csp.py', ['--output', str(out / 'data-form-policy-receipt.json')]),
    ]:
        if not measured(label, [sys.executable, driver, '--binary', jsrun, *options], 110):
            raise RuntimeError('Native regression failed: ' + label)
    if args.transfer_acceptance:
        for label, driver in [('shared-boundaries', 'tests/test_shared_worker_boundaries.py'),
                              ('shared-transfer', 'tests/test_shared_worker_transfer_lifecycle.py'),
                              ('shared-return-owner', 'tests/test_shared_worker_return_owner.py'),
                              ('shared-return-burst', 'tests/test_shared_worker_return_burst.py')]:
            if not measured(label, [sys.executable, driver, '--binary', jsrun,
                            '--output', str(out / (label + '-receipt.json'))], 110):
                raise RuntimeError('Native transferred-port acceptance failed: ' + label)
    if not measured('parser-policy-gui', [sys.executable, 'tests/test_parser_handler_csp.py', '--binary', lumen,
                     '--native-gui', '--output', str(out / 'parser-policy-gui-receipt.json')], 110):
        raise RuntimeError('Actual Mac GUI parser-policy regression failed')
    if icu_record:
        for suite in ('core', 'boundaries', 'transfer', 'return-owner', 'burst'):
            if not measured('shared-gui-' + suite, [sys.executable, 'tests/run_shared_worker_gui.py',
                            '--binary', lumen, '--sha256', summary['artifacts']['lumen']['sha256'],
                            '--suite', suite, '--output', str(out / ('shared-gui-' + suite + '-receipt.json'))], 110):
                raise RuntimeError('Actual Mac GUI SharedWorker acceptance failed: ' + suite)
    for name, digest in sources.items():
        if sha(root / name) != digest:
            raise RuntimeError('Production source changed during candidate validation: ' + name)
    linked = subprocess.check_output(['otool', '-L', lumen], text=True)
    (out / 'linked-libraries.txt').write_text(linked)
    summary['linkedV8'] = [line.strip() for line in linked.splitlines() if 'v8' in line.lower()]
    if icu_record:
        tools_environment = dict(os.environ, DEVELOPER_DIR='/Library/Developer/CommandLineTools')
        native_icu = (prefix / 'lib/libicuuc.dylib').resolve()
        if str(native_icu) not in linked:
            raise RuntimeError('Final executable is not linked to the pinned ICU79 library')
        for label, target in [('binary', Path(lumen)), ('url-object', root / build / 'src/base/url.c.o')]:
            symbols = subprocess.check_output(['nm', '-u', str(target)], text=True, timeout=20, env=tools_environment)
            (out / (label + '-imports.txt')).write_text(symbols)
            idna = [line.split()[-1] for line in symbols.splitlines() if 'uidna_' in line]
            if ('_uidna_openUTS46_79' not in idna or '_uidna_nameToASCII_UTF8_79' not in idna
                    or any(not name.endswith('_79') for name in idna)):
                raise RuntimeError('Cached URL object or final executable did not bind ICU79: ' + label)
            summary['icuAttestation'][label + 'UIDNAImports'] = idna
            summary['icuAttestation'][label + 'ImportsSHA256'] = sha(out / (label + '-imports.txt'))
        for library in icu_record['libraries']:
            if sha(prefix / library['library']) != library['relocatedSHA256']:
                raise RuntimeError('A staged ICU library changed during native acceptance')
        if str(Path('/opt/homebrew/opt/icu4c').resolve()) != icu_record['originalHostICUPrefix']:
            raise RuntimeError('The host ICU alias changed during native acceptance')
        for name, digest in v8_hashes.items():
            if sha(name) != digest:
                raise RuntimeError('A prebuilt V8 binary changed during ICU acceptance')
        summary['icuAttestation']['hostAliasUnchanged'] = True
        summary['icuAttestation']['prebuiltV8Unchanged'] = True
    summary['passed'] = True
except Exception as error:
    summary['error'] = type(error).__name__ + ': ' + str(error)
finally:
    summary.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - started, 3))
    save()
    print(json.dumps({
        'passed': summary['passed'], 'source': head, 'summary': str((out / 'summary.json').relative_to(root)),
        'lumenSHA256': summary['artifacts'].get('lumen', {}).get('sha256'),
        'jsrunSHA256': summary['artifacts'].get('jsrun', {}).get('sha256'),
        'stagesPassed': sum(stage['passed'] for stage in summary['stages']), 'stagesTotal': len(summary['stages']),
        'error': summary.get('error'), 'durationSeconds': summary['durationSeconds'],
    }), flush=True)
raise SystemExit(0 if summary['passed'] else 1)

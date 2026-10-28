#!/usr/bin/env python3
"""Isolated WPT entry point for the explicitly authorized iPad Simulator.

Install beside full_runner.py and ipad_adapter.py. The Mac runner is imported
without mutation. Only signed build-sim/Lumen.app/Lumen may be executed after
its installed counterpart matches. Cross-platform comparisons are functional
compatibility evidence, never normalized hardware-performance comparisons.
"""
import argparse
from collections import Counter
import fcntl
import json
import os
from pathlib import Path
import platform
import plistlib
import signal
import stat
import subprocess
import threading
import time
import urllib.parse

import full_runner as core
from ipad_adapter import AUTHORIZED_BUNDLE, AUTHORIZED_DEVICE, IpadAdapter


PLATFORM = 'ipad-simulator'
RESOURCE_NOTE = ('iPad Simulator app process tree on the Mac; shared CoreSimulator and WindowServer '
                 'overhead excluded. These are not physical iPad measurements or same-IPA-on-Mac evidence.')
COMMON_SIGNATURE_FIELDS = ('wpt_revision', 'manifest_sha256', 'inventory_sha256', 'source_fixture',
                           'server_config_sha256', 'fixture_manifest_sha256', 'selection', 'schedule_sha256',
                           'reporter_sha256', 'monitor_sha256', 'origin', 'secure_origin', 'h2_origin',
                           'timeout_s', 'long_timeout_s', 'strict_infrastructure')


def allowed_target(device, bundle):
    if device.upper() != AUTHORIZED_DEVICE or bundle != AUTHORIZED_BUNDLE:
        raise ValueError('Only the explicitly authorized iPad UUID and Lumen bundle may be selected')


def signed_binary(repo):
    path = (Path(repo) / 'build-sim/Lumen.app/Lumen').resolve(strict=True)
    if not path.is_file():
        raise ValueError('Signed simulator app executable is missing; build and install the matching app first')
    return path


def bundle_tree(app):
    """Record entries without following symlink directories or reading file bytes."""
    app = Path(app).resolve(strict=True)
    entries = {}

    def record(path):
        value = path.lstat()
        if not (stat.S_ISREG(value.st_mode) or stat.S_ISDIR(value.st_mode) or stat.S_ISLNK(value.st_mode)):
            raise ValueError('Unexpected special file in iPad app bundle: ' + str(path))
        identity = {'device': value.st_dev, 'inode': value.st_ino, 'mode': value.st_mode,
                    'size': value.st_size, 'mtime_ns': value.st_mtime_ns, 'ctime_ns': value.st_ctime_ns}
        if stat.S_ISLNK(value.st_mode):
            if not path.resolve(strict=True).is_relative_to(app):
                raise ValueError('iPad bundle resource symlink escapes the app: ' + str(path))
            identity['link_target'] = os.readlink(path)
        entries[path.relative_to(app).as_posix()] = identity

    def failed(error):
        raise error

    record(app)
    for directory, directories, files in os.walk(app, followlinks=False, onerror=failed):
        for name in sorted(directories + files):
            record(Path(directory) / name)
    return entries


def check_bundle_guards(guards):
    for app, expected in guards.items():
        try:
            current = bundle_tree(app)
        except OSError as exc:
            raise ValueError('Signed iPad bundle became unreadable; use a fresh cohort: ' + app) from exc
        if current != expected:
            raise ValueError('Signed iPad bundle changed; use a fresh cohort: ' + app)


def verify_signed_bundles(source, installed):
    """Validate both resource seals and match every relative bundle entry once."""
    apps = {'source': Path(source).resolve(strict=True), 'installed': Path(installed).resolve(strict=True)}
    guards = {str(app): bundle_tree(app) for app in apps.values()}
    manifests, critical = {}, {}
    for name, app in apps.items():
        info = plistlib.loads((app / 'Info.plist').read_bytes())
        if info.get('CFBundleIdentifier') != AUTHORIZED_BUNDLE or info.get('CFBundleExecutable') != 'Lumen':
            raise ValueError('Signed iPad app metadata does not identify the authorized Lumen executable')
        if 'iPhoneSimulator' not in info.get('CFBundleSupportedPlatforms', []):
            raise ValueError('Signed iPad app must explicitly declare the simulator platform')
        envelope = app / '_CodeSignature/CodeResources'
        if not envelope.is_file():
            raise ValueError('Signed iPad app resource envelope is missing: ' + str(envelope))
        checked = subprocess.run(['codesign', '--verify', '--strict', '--deep', str(app)],
                                 capture_output=True, text=True, timeout=60, check=False)
        if checked.returncode:
            raise ValueError('iPad app code-signature verification failed for ' + name + ': ' + checked.stderr.strip()[:2000])
        contents = {}
        for relative, identity in sorted(guards[str(app)].items()):
            mode = identity['mode']
            if stat.S_ISREG(mode):
                contents[relative] = {'kind': 'file', 'sha256': core.file_digest(app / relative)}
            elif stat.S_ISLNK(mode):
                contents[relative] = {'kind': 'symlink', 'target': identity['link_target']}
            else:
                contents[relative] = {'kind': 'directory'}
        manifests[name] = contents
        critical[name] = {relative: contents[relative]['sha256']
                          for relative in ('Lumen', 'Info.plist', '_CodeSignature/CodeResources', 'cacert.pem', 'icudtl.dat')
                          if relative in contents and contents[relative]['kind'] == 'file'}
    check_bundle_guards(guards)
    if manifests['source'] != manifests['installed']:
        names = sorted(set(manifests['source']) | set(manifests['installed']))
        differences = [name for name in names if manifests['source'].get(name) != manifests['installed'].get(name)]
        raise ValueError('Installed iPad bundle contents differ from the source app: ' + ', '.join(differences[:10]))
    proof = {'verification': 'codesign --verify --strict --deep', 'source_verified': True, 'installed_verified': True,
             'source_app': str(apps['source']), 'installed_app': str(apps['installed']),
             'matching_bundle_contents_sha256': core.digest(manifests['source']),
             'bundle_entry_count': len(manifests['source']), 'critical_files_sha256': critical,
             'scope': 'Code-signature and sealed-resource validity plus identical bundle contents; not distribution trust, notarization, or device IPA compatibility.'}
    return proof, guards


def server_alive(fixture):
    pid, identity = fixture.get('server_pid'), fixture.get('server_start_identity')
    if type(pid) is not int or pid <= 0 or not isinstance(identity, str) or not identity:
        raise ValueError('Fixture has no verified server identity')
    result = subprocess.run(['ps', '-p', str(pid), '-o', 'lstart='], capture_output=True,
                            text=True, timeout=5, check=False)
    if result.returncode or result.stdout.strip() != identity:
        raise ValueError('WPT server changed; refresh the fixture and use a new iPad cohort')


def validate_fixture(args, inventory):
    fixture = json.loads(args.fixture_manifest.read_text())
    if fixture.get('fixture_ready') is not True or fixture.get('server_identity_error') is not None:
        raise ValueError('The required fixture readiness record did not pass cleanly')
    expected = {'wpt_revision': inventory['wpt_revision'], 'manifest_sha256': inventory['manifest_sha256'],
                'server_config_sha256': core.file_digest(args.server_config),
                'ca_sha256': core.file_digest(args.ca_file),
                'reporter_sha256': core.file_digest(Path(core.__file__).with_name('testharnessreport.js'))}
    for key, value in expected.items():
        if fixture.get(key) != value:
            raise ValueError('Fixture evidence differs from iPad inputs: ' + key)
    for origin, scheme, transport in ((args.origin, 'http', 'http'),
                                      (args.secure_origin, 'https', 'https'), (args.h2_origin, 'https', 'h2')):
        parts = urllib.parse.urlsplit(origin)
        if parts.scheme != scheme or parts.hostname != fixture.get('browser_host') or parts.port not in fixture.get('resolved_ports', {}).get(transport, []):
            raise ValueError('iPad origin is outside the verified fixture: ' + origin)
    server_alive(fixture)
    return fixture


def unsafe_result(result, strict):
    """A failed assertion or ordinary browser timeout is not an infra error."""
    reasons = []
    if result.get('status') == 'INFRA_ERROR':
        reasons.append('infrastructure error')
    attached = (result.get('reporter_state') or {}).get('status') == 'ATTACHED'
    if not attached and (strict or result.get('status') not in ('TIMEOUT', 'CRASH', 'ERROR')):
        reasons.append('synchronous reporter did not attach')
    cleanup = result.get('cleanup') or {}
    if cleanup.get('error') or cleanup.get('profile_error') or cleanup.get('stopped') is False:
        reasons.append('owned iPad app or profile cleanup failed')
    if core.measurement_failure(result):
        reasons.append('resource collector failed')
    return reasons


def identity_set(binary, installed):
    return {'signed_source': core.binary_identity(binary), 'installed_app': core.binary_identity(installed)}


def input_guards(paths):
    result = {}
    for path in paths:
        path = Path(path).resolve(strict=True)
        before = core.binary_identity(path)
        sha = core.file_digest(path)
        if before != core.binary_identity(path):
            raise ValueError('iPad input changed while hashing: ' + str(path))
        result[str(path)] = {'identity': before, 'sha256': sha}
    return result


def check_guards(guards):
    for path, guard in guards.items():
        if core.binary_identity(path) != guard['identity']:
            raise ValueError('iPad execution input changed; use a fresh cohort: ' + path)


def ensure_fresh_or_matching(output, signature, resume):
    meta_path = output / 'run.json'
    if meta_path.exists():
        if not resume:
            raise ValueError('iPad output exists; use --resume or a fresh directory')
        meta = json.loads(meta_path.read_text())
        if meta['signature'] != signature:
            raise ValueError('iPad resume identity changed; use a fresh iPad cohort')
        if meta.get('invocation_open'):
            raise ValueError('Prior invocation did not record final cleanup; inspect the exact iPad app before using a fresh cohort')
        unsafe_stops = {key: value for key, value in meta.items()
                        if key.startswith('stopped_for_') and key != 'stopped_for_time_budget' and value}
        if unsafe_stops:
            raise ValueError('Unsafe prior iPad run is preserved for diagnosis; use a fresh corrected cohort')
        core.verified_schedule(output, signature)
        return meta
    if resume:
        raise ValueError('No existing iPad run to resume')
    if output.exists() and any(output.iterdir()):
        raise ValueError('iPad output is not fresh; no prior files are overwritten')
    return None


def run_command(args):
    if platform.system() != 'Darwin':
        raise ValueError('Run only on the authorized Mac through devinmac3d')
    allowed_target(args.device, args.bundle_id)
    lock_path = args.repo / 'output/wpt' / ('ipad-' + AUTHORIZED_DEVICE + '.lock')
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return run_exclusive(args)


def run_exclusive(args):
    if platform.system() != 'Darwin':
        raise ValueError('Run only on the authorized Mac through devinmac3d')
    allowed_target(args.device, args.bundle_id)
    if not args.host_map:
        raise ValueError('Supply the validated exact native WPT host map')
    if args.timeout <= 0 or args.long_timeout <= 0 or (args.max_seconds is not None and args.max_seconds <= 0):
        raise ValueError('Watchdogs and optional invocation budget must be positive')
    inventory = json.loads(args.inventory.read_text())
    kinds = set(args.types.split(','))
    if not kinds or kinds - core.SUPPORTED_EXECUTION_TYPES:
        raise ValueError('The iPad adapter executes testharness and official test262 wrappers only')
    selection, selection_descriptor = core.select_schedule(inventory, args.types, args.prefix, args.offset, args.limit)
    source_fixture = core.source_fixture_signature(inventory)
    fixture = validate_fixture(args, inventory)
    route_digest = core.verify_reporter_route(args.origin)
    binary = signed_binary(args.repo)
    guards = input_guards([args.inventory, args.server_config, args.fixture_manifest, args.ca_file,
                           Path(__file__), Path(__file__).with_name('ipad_adapter.py'), Path(core.__file__),
                           Path(core.__file__).with_name('testharnessreport.js'), args.repo / 'tools/monitor_process.py'])
    os.environ.update(LUMEN_HOST_MAP=args.host_map, LUMEN_HOST_MAP_MODE='exact', SSL_CERT_FILE=str(args.ca_file))
    adapter = IpadAdapter(args.device, args.bundle_id, runtime=core)
    adapter._verify_binary(binary)
    bundle_proof, bundle_guards = verify_signed_bundles(binary.parent, adapter.app)
    initial_identities = identity_set(binary, adapter.binary)
    binary_hash = core.file_digest(binary)
    if identity_set(binary, adapter.binary) != initial_identities:
        raise ValueError('Signed or installed app changed during verification')
    strict = not args.allow_reportless_failures
    signature = {'wpt_revision': inventory['wpt_revision'], 'manifest_sha256': inventory['manifest_sha256'],
                 'inventory_sha256': core.file_digest(args.inventory), 'source_fixture': source_fixture,
                 'server_config_sha256': core.file_digest(args.server_config),
                 'fixture_manifest_sha256': core.file_digest(args.fixture_manifest),
                 'selection': selection_descriptor, 'schedule_sha256': core.digest(selection),
                 'engine': 'lumen', 'execution_platform': PLATFORM, 'binary_sha256': binary_hash,
                 'runner_sha256': core.file_digest(Path(__file__)),
                 'core_runner_sha256': core.file_digest(Path(core.__file__)),
                 'reporter_sha256': core.file_digest(Path(core.__file__).with_name('testharnessreport.js')),
                 'verified_http_reporter_sha256': route_digest,
                 'reporter_delivery': 'official WPT /resources/testharnessreport.js exact file alias; no prefile injection',
                 'monitor_sha256': core.file_digest(args.repo / 'tools/monitor_process.py'),
                 'native_fixture': core.native_fixture_signature(), 'adapter': adapter.metadata,
                 'signed_bundle_provenance': bundle_proof,
                 'execution_inputs_sha256': {path: value['sha256'] for path, value in guards.items()},
                 'origin': args.origin, 'secure_origin': args.secure_origin, 'h2_origin': args.h2_origin,
                 'timeout_s': args.timeout, 'long_timeout_s': args.long_timeout, 'strict_infrastructure': strict}
    meta = ensure_fresh_or_matching(args.output, signature, args.resume)
    if meta is None:
        meta = {'signature': signature, 'execution_platform': PLATFORM, 'host_platform': platform.platform(),
                'machine': platform.machine(), 'started_at_unix_ms': round(time.time() * 1000),
                'binary': str(binary), 'installed_binary': str(adapter.binary), 'adapter': adapter.metadata,
                'signed_bundle_provenance': bundle_proof,
                'full_inventory_counts': inventory['test_variants_by_type'],
                'inventory_coverage_by_type': core.inventory_coverage(inventory, selection),
                'scheduled_files': len(selection), 'supported_execution_types': sorted(core.SUPPORTED_EXECUTION_TYPES),
                'is_full_suite_schedule': len(selection) == len(inventory['tests']),
                'resource_scope': RESOURCE_NOTE,
                'automation_limitation': 'No testdriver actions, reference comparison, manual tests, or device IPA execution.',
                'known_fixture_limitations': fixture.get('known_limitations')}
        args.output.mkdir(parents=True, exist_ok=True)
        core.write_json(args.output / 'run.json', meta)
        core.write_json(args.output / 'schedule.json', selection)
    accounting = core.restore_accounting(args.output / 'results.jsonl', selection)
    stop, began = threading.Event(), time.monotonic()
    meta.update(invocation_open=True, last_invocation_started_unix_ms=round(time.time() * 1000),
                last_invocation_budget_s=args.max_seconds)
    meta.pop('stopped_for_time_budget', None)
    core.write_json(args.output / 'run.json', meta)
    old_handlers = {}

    def interrupted(signum, _frame):
        meta['last_stop_signal'] = signum
        stop.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        old_handlers[sig] = signal.signal(sig, interrupted)
    capture_pending = args.screenshot_first
    case_in_flight = False
    try:
        with (args.output / 'results.jsonl').open('a') as output:
            for index, test in enumerate(selection):
                if stop.is_set():
                    break
                if (test['type'], test['test']) in accounting.seen:
                    continue
                if args.max_seconds is not None and time.monotonic() - began >= args.max_seconds:
                    meta['stopped_for_time_budget'] = True
                    break
                server_alive(fixture)
                check_guards(guards)
                check_bundle_guards(bundle_guards)
                if identity_set(binary, adapter.binary) != initial_identities:
                    meta['stopped_for_binary_change'] = True
                    break
                prefix = args.output / ('case-' + format(index, '06d') + '-' + core.digest([test['type'], test['test']])[:10])
                base = {'type': test['type'], 'test': test['test'], 'url': core.test_url(test, args),
                        'sequence': index, 'execution_platform': PLATFORM, 'started_at_unix_ms': round(time.time() * 1000)}
                adapter.capture_next = capture_pending
                capture_pending = False
                timeout = args.long_timeout if test.get('timeout') == 'long' else args.timeout
                case_in_flight = True
                try:
                    result = adapter.run(binary, base['url'], timeout, prefix, reporter=None)
                except Exception as exc:
                    result = {'status': 'INFRA_ERROR', 'message': type(exc).__name__ + ': ' + str(exc), 'subtests': [],
                              'cleanup': {'stopped': None, 'error': 'Adapter raised before returning cleanup evidence'}}
                case_in_flight = False
                if identity_set(binary, adapter.binary) != initial_identities:
                    core.write_json(prefix.with_suffix('.invalid-binary-result.json'), result)
                    result.update(status='INFRA_ERROR', message='Signed/installed app changed during case; assertions quarantined', subtests=[])
                    meta['stopped_for_binary_change'] = True
                try:
                    check_guards(guards)
                    check_bundle_guards(bundle_guards)
                except ValueError as exc:
                    core.write_json(prefix.with_suffix('.invalid-input-result.json'), result)
                    result.update(status='INFRA_ERROR', message='Execution input changed during case; assertions quarantined', subtests=[])
                    meta['stopped_for_input_change'] = str(exc)
                base.update(result)
                output.write(json.dumps(base, ensure_ascii=True) + '\n')
                output.flush()
                os.fsync(output.fileno())
                accounting.add(base)
                core.write_json(args.output / 'summary.json', accounting.value())
                reasons = unsafe_result(base, strict)
                if reasons:
                    meta['stopped_for_unsafe_result'] = {'test': base['test'], 'reasons': reasons}
                    stop.set()
                print(json.dumps({'completed': len(accounting.seen), 'scheduled': len(selection),
                                  'execution_platform': PLATFORM, 'test': base['test'], 'status': base['status'],
                                  'subtests': dict(Counter(sub['status'] for sub in base['subtests']))}), flush=True)
    except Exception as exc:
        meta['stopped_for_controller_error'] = type(exc).__name__ + ': ' + str(exc)
        raise
    except BaseException as exc:
        meta['stopped_for_abnormal_controller_exit'] = type(exc).__name__ + ': ' + str(exc)
        if case_in_flight:
            meta['cleanup_unverified'] = 'Adapter did not return cleanup evidence before the abnormal controller exit'
        raise
    finally:
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)
        meta.update(invocation_open=case_in_flight, last_checkpoint_unix_ms=round(time.time() * 1000),
                    completed_result_rows=len(accounting.seen),
                    finished_schedule=len(accounting.seen) == len(selection))
        core.write_json(args.output / 'summary.json', accounting.value())
        core.write_json(args.output / 'run.json', meta)
    return 2 if any(value for key, value in meta.items()
                    if key.startswith('stopped_for_') and key != 'stopped_for_time_budget') else 0


def invocation_is_open(meta):
    if meta.get('invocation_open'):
        return True
    started, checkpoint = meta.get('last_invocation_started_unix_ms'), meta.get('last_checkpoint_unix_ms')
    return started is not None and (checkpoint is None or checkpoint < started)


def comparison_run_state(meta, summary):
    recorded, scheduled = summary['recorded_files'], summary['scheduled_files']
    return {'invocation_open': meta.get('invocation_open'), 'inferred_active_invocation': invocation_is_open(meta),
            'last_invocation_started_unix_ms': meta.get('last_invocation_started_unix_ms'),
            'last_checkpoint_unix_ms': meta.get('last_checkpoint_unix_ms'),
            'completed_result_rows': recorded, 'all_scheduled_rows_recorded': recorded == scheduled,
            'finished_schedule': meta.get('finished_schedule', False),
            'stops': {key: value for key, value in meta.items() if key.startswith('stopped_for_')},
            'cleanup_unverified': meta.get('cleanup_unverified')}


def verify_comparison(ipad_meta, chrome_meta):
    if invocation_is_open(ipad_meta) or invocation_is_open(chrome_meta):
        raise ValueError('An active or unclosed invocation cannot be compared; wait for its final checkpoint')
    left, right = ipad_meta['signature'], chrome_meta['signature']
    if left.get('engine') != 'lumen' or left.get('execution_platform') != PLATFORM or right.get('engine') != 'chromium':
        raise ValueError('This comparison requires iPad Lumen and the separately labelled Mac Chromium control')
    if not str(chrome_meta.get('platform', '')).startswith(('macOS-', 'Darwin-')):
        raise ValueError('Chromium control is not identified as the authorized Mac run')
    if left.get('core_runner_sha256') != right.get('runner_sha256'):
        raise ValueError('iPad and Chromium must share the same reviewed core collector/schedule implementation')
    for key in COMMON_SIGNATURE_FIELDS:
        if left[key] != right[key]:
            raise ValueError('Cross-platform compatibility comparison requires identical ' + key)


def compare_command(args):
    comparison_inputs = [directory / name for directory in (args.ipad, args.chromium)
                         for name in ('run.json', 'schedule.json', 'results.jsonl')]
    identities = {str(path): core.binary_identity(path) for path in comparison_inputs}
    ipad_meta = json.loads((args.ipad / 'run.json').read_text())
    chrome_meta = json.loads((args.chromium / 'run.json').read_text())
    verify_comparison(ipad_meta, chrome_meta)
    schedule = core.verified_schedule(args.ipad, ipad_meta['signature'])
    core.verified_schedule(args.chromium, chrome_meta['signature'])
    ipad, chromium, compared = core.compare_streams(args.ipad / 'results.jsonl', args.chromium / 'results.jsonl', schedule)
    if identities != {str(path): core.binary_identity(path) for path in comparison_inputs}:
        raise ValueError('Run inputs changed during comparison; no possibly active snapshot is published')
    states = {engine: comparison_run_state(meta, summary)
              for engine, meta, summary in (('ipad_lumen', ipad_meta, ipad), ('mac_chromium', chrome_meta, chromium))}
    diagnostic = any(not state['finished_schedule'] or not state['all_scheduled_rows_recorded'] or
                     any(state['stops'].values()) or state['cleanup_unverified'] for state in states.values())
    report = {'comparison_kind': 'cross-platform functional compatibility only',
              'comparison_status': 'diagnostic-partial-or-stopped' if diagnostic else 'completed-pair',
              'platforms': {'lumen': PLATFORM, 'chromium': 'macOS'},
              'wpt_revision': ipad_meta['signature']['wpt_revision'], 'scheduled_files': len(schedule),
              'ipad_lumen': ipad, 'mac_chromium': chromium, 'parity': compared,
              'ipad_adapter': ipad_meta['adapter'], 'chromium_version': chrome_meta.get('browser_version'),
              'ipad_signed_bundle_provenance': ipad_meta['signature']['signed_bundle_provenance'],
              'binary_sha256': {'ipad_lumen': ipad_meta['signature']['binary_sha256'],
                                'mac_chromium': chrome_meta['signature']['binary_sha256']},
              'run_state': states,
              'resource_comparison': 'Not performed; simulator app-only telemetry and Mac Chrome process-tree telemetry have different scopes.',
              'resource_scope': RESOURCE_NOTE, 'inventory_coverage_by_type': ipad_meta['inventory_coverage_by_type'],
              'note': 'This file must not be added to the Mac coordinator aggregate. Unrun assertions and unsupported classes never count as passes.'}
    core.write_json(args.output, report)
    print(json.dumps({'comparison': str(args.output), 'platforms': report['platforms'],
                      'scheduled_files': len(schedule), 'parity_counts': compared['counts'],
                      'chromium_pass_retention': compared['chromium_pass_retention']}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    run = commands.add_parser('run')
    run.add_argument('--repo', type=Path, default=Path.cwd())
    run.add_argument('--inventory', type=Path, default=Path('output/wpt/inventory.json'))
    run.add_argument('--server-config', type=Path, default=Path('output/wpt/server.config.json'))
    run.add_argument('--fixture-manifest', type=Path, required=True)
    run.add_argument('--ca-file', type=Path, default=Path('output/wpt/upstream/tools/certs/cacert.pem'))
    run.add_argument('--host-map', default=os.environ.get('LUMEN_HOST_MAP'))
    run.add_argument('--device', default=AUTHORIZED_DEVICE)
    run.add_argument('--bundle-id', default=AUTHORIZED_BUNDLE)
    run.add_argument('--types', default='testharness')
    run.add_argument('--prefix', action='append')
    run.add_argument('--offset', type=int, default=0)
    run.add_argument('--limit', type=int, default=6)
    run.add_argument('--timeout', type=float, default=15)
    run.add_argument('--long-timeout', type=float, default=65)
    run.add_argument('--max-seconds', type=float)
    run.add_argument('--allow-reportless-failures', action='store_true')
    run.add_argument('--screenshot-first', action='store_true', help='Diagnostic screenshot only; never a reftest pass')
    run.add_argument('--origin', default='http://web-platform.test:8000')
    run.add_argument('--secure-origin', default='https://web-platform.test:18443')
    run.add_argument('--h2-origin', default='https://web-platform.test:9000')
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--resume', action='store_true')
    run.set_defaults(func=run_command)
    compare = commands.add_parser('compare')
    compare.add_argument('--ipad', type=Path, required=True)
    compare.add_argument('--chromium', type=Path, required=True)
    compare.add_argument('--output', type=Path, required=True)
    compare.set_defaults(func=compare_command)
    args = parser.parse_args()
    if args.command == 'run':
        args.repo = args.repo.resolve(strict=True)
        for key in ('inventory', 'server_config', 'fixture_manifest', 'ca_file', 'output'):
            setattr(args, key, (args.repo / getattr(args, key)).resolve())
        args.prefix = args.prefix if args.prefix is not None else ['/url/']
    raise SystemExit(args.func(args) or 0)


if __name__ == '__main__':
    main()

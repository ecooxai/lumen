#!/usr/bin/env python3
"""Mac-only, sequential Lumen/Chromium WPT cohorts with bounded shards.

Run only on the authorized Mac through devinmac3d. This controller does not
start WPT servers, change system DNS, copy login state, or implement test APIs.
The installed v3 full_runner.py owns browser launch, reporting and 1s telemetry.
"""
import argparse
from collections import Counter
from contextlib import redirect_stderr, redirect_stdout
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import urllib.parse


SUPPORTED = ('testharness', 'test262')
SPKI = 'WXjEgm6G9rSgRQQVGbD5q+zOszPmv3ZBSfaIAMv+bY8='
SMOKES = (('testharness', '/url/'), ('test262', '/infrastructure/test262/'))
REFERENCE_POLICY_SHA256 = '9e4954758177647d82cda5a26d39307cf07bcbfde21fc2e5349cd2065b29f8af'
REFERENCE_POLICY = 'functional_only_cft156_known_unsignaled_watchdog_exit2'


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=True) + '\n')
    temporary.replace(path)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def file_digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def identity(path):
    value = Path(path).stat()
    return [value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns]


def record_file(path):
    path = Path(path).resolve(strict=True)
    before = identity(path)
    result = {'path': str(path), 'sha256': file_digest(path), 'identity': before}
    if identity(path) != before:
        raise ValueError('Input changed while hashing: ' + str(path))
    return result


def assert_files_unchanged(records, content=False):
    for record in records.values():
        path = Path(record['path'])
        if identity(path) != record['identity'] or (content and file_digest(path) != record['sha256']):
            raise ValueError('Cohort input changed; create a new generation: ' + str(path))


def snapshot_native(source):
    """Keep executable-relative ICU/CA resources valid by copying adjacently."""
    source = Path(source).resolve(strict=True)
    original = record_file(source)
    target = source.with_name('lumen-wpt-' + original['sha256'][:16])
    if target == source:
        raise ValueError('Pass the original build/lumen, not its snapshot, to prepare a cohort')
    if target.exists():
        if not target.is_file() or file_digest(target) != original['sha256']:
            raise ValueError('Existing native snapshot conflicts with its content name: ' + str(target))
        if target.stat().st_mode & 0o222:
            raise ValueError('Existing native snapshot is writable: ' + str(target))
    else:
        descriptor, temporary_name = tempfile.mkstemp(prefix='.lumen-wpt-copy-', dir=source.parent)
        os.close(descriptor)
        temporary = Path(temporary_name)
        try:
            shutil.copy2(source, temporary)
            if identity(source) != original['identity'] or file_digest(temporary) != original['sha256']:
                raise ValueError('Native executable changed during snapshot preparation')
            temporary.chmod(stat.S_IMODE(temporary.stat().st_mode) & ~0o222)
            if not os.access(temporary, os.X_OK):
                raise ValueError('Native snapshot is not executable')
            # One cohort controller holds its lock; an existing identical target
            # is accepted, but conflicting content is never overwritten.
            try:
                os.link(temporary, target)
            except FileExistsError:
                if file_digest(target) != original['sha256'] or target.stat().st_mode & 0o222:
                    raise ValueError('Concurrent native snapshot conflicts: ' + str(target))
        finally:
            temporary.unlink(missing_ok=True)
    return {'source': original, 'snapshot': record_file(target),
            'resource_policy': 'Adjacent to original executable; unchanged repository cwd; validated by paired smoke'}


def load_runner(path):
    spec = importlib.util.spec_from_file_location('lumen_cohort_runner', path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    return runner


def validate_fixture(fixture, inventory, args):
    if fixture.get('fixture_ready') is not True or fixture.get('server_identity_error') is not None:
        raise ValueError('Fixture readiness did not pass cleanly')
    expected = {'wpt_revision': inventory['wpt_revision'], 'manifest_sha256': inventory['manifest_sha256'],
                'server_config_sha256': file_digest(args.server_config), 'ca_sha256': file_digest(args.ca_file),
                'reporter_sha256': file_digest(args.runner.with_name('testharnessreport.js'))}
    for key, value in expected.items():
        if fixture.get(key) != value:
            raise ValueError('Fixture evidence differs from current inputs: ' + key)
    for origin, scheme, transport in ((args.origin, 'http', 'http'),
                                      (args.secure_origin, 'https', 'https'), (args.h2_origin, 'https', 'h2')):
        parts = urllib.parse.urlsplit(origin)
        if parts.scheme != scheme or parts.hostname != fixture.get('browser_host') or parts.port not in fixture.get('resolved_ports', {}).get(transport, []):
            raise ValueError('Requested origin is outside the verified fixture: ' + origin)
    observed = {'server_pid': fixture.get('server_pid'), 'server_start_identity': fixture.get('server_start_identity')}
    verify_server_live(observed)
    return observed


def verify_server_live(fixture_identity):
    pid, expected = fixture_identity.get('server_pid'), fixture_identity.get('server_start_identity')
    if type(pid) is not int or pid <= 0 or not isinstance(expected, str) or not expected:
        raise ValueError('Fixture has no verifiable server process identity')
    observed = subprocess.run(['ps', '-p', str(pid), '-o', 'lstart='], capture_output=True,
                              text=True, timeout=5, check=False)
    if observed.returncode or observed.stdout.strip() != expected:
        raise ValueError('Verified WPT server exited or was replaced; create a new fixture/cohort')


def plan_units(runner, inventory, kinds, prefixes, offset, limit, shard_size, smoke_count):
    if not kinds or len(set(kinds)) != len(kinds) or set(kinds) - set(SUPPORTED):
        raise ValueError('Use distinct supported types: testharness,test262')
    if shard_size <= 0 or smoke_count <= 0:
        raise ValueError('Shard size and smoke count must be positive')
    units, selected_counts = [], Counter()

    def unit(name, phase, kind, selected_prefixes, start, count):
        rows, selection = runner.select_schedule(inventory, kind, selected_prefixes, start, count)
        return {'name': name, 'phase': phase, 'type': kind, 'prefixes': list(selected_prefixes),
                'offset': start, 'limit': count, 'selected_files': len(rows),
                'selection': selection, 'schedule_sha256': runner.digest(rows)}

    for kind, prefix in SMOKES:
        # The URL smoke establishes native snapshot launch; test262 additionally
        # proves the official iframe wrapper when that class is requested.
        if kind == 'testharness' or kind in kinds:
            units.append(unit('smoke-' + kind, 'smoke', kind, [prefix], 0, smoke_count))
    for kind in kinds:
        rows, descriptor = runner.select_schedule(inventory, kind, prefixes, offset, limit)
        selected_counts[kind] = len(rows)
        for start in range(0, len(rows), shard_size):
            count = min(shard_size, len(rows) - start)
            # Filter/sort the 130k-entry inventory once per class, not once for
            # every shard. This descriptor matches the runner's direct slice.
            selection = dict(descriptor, offset=offset + start, limit=count, selected_files=count)
            units.append({'name': kind + '-' + format(offset + start, '06d'), 'phase': 'broad',
                          'type': kind, 'prefixes': list(prefixes), 'offset': offset + start,
                          'limit': count, 'selected_files': count, 'selection': selection,
                          'schedule_sha256': runner.digest(rows[start:start + count])})
    coverage = {kind: {'inventory_variants': count, 'selected_broad_variants': selected_counts[kind],
                       'outside_selection_variants': count - selected_counts[kind],
                       'executor_available': kind in SUPPORTED,
                       'plan': 'SCHEDULED' if selected_counts[kind] else 'NOTRUN'}
                for kind, count in sorted(inventory['test_variants_by_type'].items())}
    return units, coverage


def row_issues(row, smoke=False):
    issues = []
    if row.get('status') == 'INFRA_ERROR':
        issues.append('infrastructure error: ' + str(row.get('message', '')))
    if (row.get('reporter_state') or {}).get('status') != 'ATTACHED' and (smoke or row.get('status') not in ('TIMEOUT', 'CRASH', 'ERROR')):
        issues.append('synchronous reporter did not attach')
    cleanup = row.get('cleanup') or {}
    if cleanup.get('error') or cleanup.get('profile_error') or cleanup.get('errors'):
        issues.append('browser/context/profile cleanup failed')
    metrics = row.get('metrics') or {}
    if metrics.get('errors') or metrics.get('telemetry_status') == 'failed' or row.get('telemetry_errors'):
        issues.append('resource collector failed')
    if smoke and (not row.get('subtests') or 'subtest_count' not in row):
        issues.append('smoke did not produce a complete assertion report')
    return issues


def load_reference_policy():
    path = Path(__file__).with_name('run_cft156_url77.py')
    if file_digest(path) != REFERENCE_POLICY_SHA256:
        raise ValueError('Reviewed functional-reference classifier changed')
    spec = importlib.util.spec_from_file_location('lumen_cohort_reference_policy', path)
    policy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(policy)
    return policy


def natural_reference_cleanup(meta):
    cleanup = meta.get('chromium_cleanup') or {}
    process = cleanup.get('process') or {}
    return bool(cleanup and not cleanup.get('errors') and not process.get('error')
                and not process.get('teardown_error') and process.get('root_exit_code') == 0
                and process.get('root_exit_observation_verified') is True
                and process.get('root_exit_before_signal_cleanup') == 0
                and process.get('sent_sigterm') is False and process.get('sent_sigkill') is False
                and process.get('process_group_gone') is True and process.get('remaining_group_members') == {})


def known_reference_incident(directory, meta, complete, issues):
    """Permit only the reviewed post-report incident; retain every failure fact."""
    expected = ('stopped_for_cleanup_failure', 'chromium final cleanup failed')
    if not complete or any(issue not in expected for issue in issues):
        return None
    if (meta.get('browser_version') or {}).get('product', '').split('/')[-1] != '156.0.8078.47':
        return None
    sessions = meta.get('chromium_sessions') or []
    if len(sessions) != 1 or not load_reference_policy().known_reference_teardown(meta):
        return None
    directory = Path(directory).resolve(strict=True)
    log = (Path(sessions[0]) / 'chromium-process.log').resolve(strict=True)
    if not log.is_relative_to(directory):
        raise ValueError('Known reference log must belong to this exact fresh run')
    before = identity(log)
    with log.open('rb') as source:
        source.seek(max(0, log.stat().st_size - 65536))
        tail = source.read(65536)
    observed = record_file(log)
    if observed['identity'] != before:
        raise ValueError('Reference watchdog log changed during inspection')
    if b'Teardown watchdog expired; recording dump and terminating.' not in tail:
        return None
    return {'classification': 'functional_only_reference_teardown_failed', 'policy': REFERENCE_POLICY,
            'cleanup_passed': False, 'performance_release_gate_passed': False,
            'cleanup_evidence': meta['chromium_cleanup'], 'browser_log': observed,
            'policy_sha256': REFERENCE_POLICY_SHA256,
            'note': 'Exact observed exit2 is retained. No signal, live owned process, per-case error, or collector failure is excused.'}


def qualify_comparison(path, cohort, reference_result, diagnostic=False):
    if not cohort.get('configuration', {}).get('allow_known_reference_teardown', False):
        return
    report = json.loads(Path(path).read_text())
    incident = (reference_result or {}).get('functional_reference_incident')
    report['reference_qualification'] = {
        'policy': REFERENCE_POLICY, 'classification': 'functional_only_reference_teardown_failed',
        'reference_performance_release_gate_passed': False,
        'current_control_cleanup_passed': bool(reference_result and not incident and not reference_result.get('issues')),
        'current_control_incident': incident, 'diagnostic_only_for_other_issue': diagnostic,
        'note': 'Functional assertions remain usable under the explicit policy; reference teardown and performance/release qualification remain failed.'}
    write_json(path, report)


class ResultWatch:
    """Watch newly committed JSONL records, retaining at most one file result."""
    def __init__(self, path, smoke):
        self.path, self.smoke = Path(path), smoke
        self.offset = self.path.stat().st_size if self.path.exists() else 0

    def first_issue(self):
        if not self.path.exists():
            return None
        with self.path.open('rb') as source:
            source.seek(self.offset)
            while True:
                position = source.tell()
                line = source.readline()
                if not line or not line.endswith(b'\n'):
                    self.offset = position  # wait for a fully committed record
                    return None
                row = json.loads(line)
                self.offset = source.tell()
                issues = row_issues(row, smoke=self.smoke)
                if issues:
                    return {'test': row.get('test'), 'status': row.get('status'), 'issues': issues}


def inspect_run(runner, directory, unit, engine, expected_hash, repair=False, allow_known_reference_teardown=False):
    meta = json.loads((directory / 'run.json').read_text())
    signature = meta['signature']
    if signature['engine'] != engine or signature['binary_sha256'] != expected_hash:
        raise ValueError('Engine identity differs from cohort: ' + str(directory))
    if signature['schedule_sha256'] != unit['schedule_sha256'] or signature['selection'] != unit['selection']:
        raise ValueError('Run selection differs from paired shard: ' + str(directory))
    if bool(signature.get('strict_infrastructure')) != (unit['phase'] == 'smoke'):
        raise ValueError('Run reporting policy differs from its cohort phase')
    schedule = runner.verified_schedule(directory, signature)
    issues = []
    stops = {key: value for key, value in meta.items() if key.startswith('stopped_for_') and value}
    for key in stops:
        if key != 'stopped_for_time_budget':
            issues.append(key)
    telemetry, reporting = Counter(), Counter()
    count = 0
    for index, row in enumerate(runner.iter_jsonl(directory / 'results.jsonl', repair=repair)):
        if index >= len(schedule) or (row['type'], row['test']) != (schedule[index]['type'], schedule[index]['test']):
            raise ValueError('Run results are not a contiguous schedule prefix')
        count += 1
        found = row_issues(row, smoke=unit['phase'] == 'smoke')
        if found and len(issues) < 20:
            issues.append({'test': row['test'], 'issues': found})
        telemetry[(row.get('metrics') or {}).get('telemetry_status', 'session_reference')] += 1
        if (row.get('reporter_state') or {}).get('status') != 'ATTACHED':
            reporting['reporter_missing_files'] += 1
        if row.get('status') == 'TIMEOUT':
            reporting['harness_timeout_files' if 'subtest_count' in row else 'external_watchdog_timeout_files'] += 1
        if row.get('status') == 'CRASH':
            reporting['crashed_files'] += 1
    chrome_cleanup = meta.get('chromium_cleanup') or {}
    if chrome_cleanup.get('errors') or (chrome_cleanup.get('process') or {}).get('error'):
        issues.append('chromium final cleanup failed')
    # Session JSONL/summary names are unique across resumes; read bounded
    # summaries, never materialize the full raw telemetry stream in memory.
    sessions = []
    for session in meta.get('chromium_sessions', []):
        summary_path = Path(session) / 'chromium.metrics-summary.json'
        if not summary_path.is_file():
            issues.append('missing Chromium telemetry summary: ' + str(summary_path))
            continue
        summary = json.loads(summary_path.read_text())
        sessions.append({'path': str(summary_path), 'telemetry_status': summary.get('telemetry_status'),
                         'samples': summary.get('samples'), 'interval_cpu_samples': summary.get('interval_cpu_samples')})
        if runner.measurement_failure({'metrics': summary}):
            issues.append('Chromium session resource collector failed')
    complete = bool(meta.get('finished_schedule')) and count == len(schedule)
    incident = None
    if engine == 'chromium' and allow_known_reference_teardown:
        if (meta.get('browser_version') or {}).get('product', '').split('/')[-1] != '156.0.8078.47':
            issues.append('Explicit functional-reference policy requires the verified CfT156 artifact')
        if not sessions:
            issues.append('Explicit functional-reference policy requires session telemetry evidence')
        if not natural_reference_cleanup(meta):
            incident = known_reference_incident(directory, meta, complete, issues)
            if incident is not None:
                issues = [issue for issue in issues if issue not in
                          ('stopped_for_cleanup_failure', 'chromium final cleanup failed')]
            else:
                issues.append('Reference cleanup is outside the explicit functional-only policy')
    return {'complete': complete, 'recorded_files': count, 'scheduled_files': len(schedule),
            'stops': stops, 'issues': issues, 'telemetry_case_status_counts': dict(telemetry),
            'reporting_counts': dict(reporting),
            'functional_reference_incident': incident,
            'chromium_sessions': sessions, 'browser_version': meta.get('browser_version')}


class Halt(RuntimeError):
    pass


class Pause(Halt):
    """An operational budget stopped work without changing browser outcomes."""


def execution_environment(args):
    environment = os.environ.copy()
    for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
        environment.pop(key, None)
    environment.update(LUMEN_HOST_MAP=args.host_map, LUMEN_HOST_MAP_MODE=args.host_map_mode,
                       SSL_CERT_FILE=str(args.ca_file))
    return environment


def environment_signature(environment):
    """Bind relevant inherited switches without logging their raw values."""
    exact = {'SSL_CERT_FILE', 'SSL_CERT_DIR', 'HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY',
             'NO_PROXY', 'http_proxy', 'https_proxy', 'all_proxy', 'no_proxy'}
    return digest({key: value for key, value in environment.items()
                   if key in exact or key.startswith(('LUMEN_', 'DYLD_', 'V8_'))})


class Controller:
    def __init__(self, args, runner, cohort, state):
        self.args, self.runner, self.cohort, self.state = args, runner, cohort, state
        self.stop_requested, self.child = False, None
        self.environment = execution_environment(args)

    def save(self, **changes):
        self.state.update(changes, updated_unix_ms=round(time.time() * 1000), controller_pid=os.getpid())
        write_json(self.args.output / 'controller.json', self.state)

    def request_stop(self, signum, _frame):
        self.stop_requested = True
        self.state['requested_signal'] = signum

    def pin_chromium_version(self, result):
        observed = result.get('browser_version')
        if not isinstance(observed, dict) or not observed.get('product') or not observed.get('revision'):
            raise Halt('Chromium did not record an identifiable Browser.getVersion result')
        pinned = self.state.get('chromium_browser_version')
        if pinned is not None and observed != pinned:
            self.state['chromium_version_drift'] = {'expected': pinned, 'observed': observed}
            raise Halt('Chromium runtime version changed; this shard cannot contribute to the cohort')
        self.state['chromium_browser_version'] = observed

    def diagnostic_comparison(self):
        """Keep functional evidence even when cleanup blocks the next launch."""
        if self.child is not None and self.child.poll() is None:
            return
        name = self.state.get('current_unit')
        if not name:
            return
        directory = self.args.output / name
        if not all((directory / engine / 'run.json').is_file() for engine in ('lumen', 'chromium')):
            return
        output = directory / 'diagnostic-comparison.json'
        try:
            with (directory / 'diagnostic-compare.log').open('w') as log, redirect_stdout(log), redirect_stderr(log):
                self.runner.compare_command(SimpleNamespace(lumen=directory / 'lumen', chromium=directory / 'chromium', output=output))
            reference = self.state.get('units', {}).get(name, {}).get('chromium')
            qualify_comparison(output, self.cohort, reference, diagnostic=True)
            self.state.setdefault('units', {}).setdefault(name, {}).update(
                diagnostic_comparison=str(output), diagnostic_comparison_sha256=file_digest(output),
                diagnostic_only=True)
        except Exception as exc:
            self.state['diagnostic_comparison_error'] = type(exc).__name__ + ': ' + str(exc)

    def launch(self, command, log_path, unit_name, stage, budget=None, watcher=None):
        if self.stop_requested:
            raise Halt('Stop requested before next stage')
        assert_files_unchanged(self.cohort['files'])
        verify_server_live(self.cohort['fixture_identity'])
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open('ab') as log:
            child = subprocess.Popen(command, cwd=self.args.repo, env=self.environment,
                                     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            self.child = child
            try:
                self.save(status='running', current_unit=unit_name, current_stage=stage,
                          child_pid=child.pid, current_log=str(log_path), command=command)
                began, signalled_at, watchdog_expired, unsafe_row = time.monotonic(), None, False, None
                while child.poll() is None:
                    if watcher is not None and unsafe_row is None:
                        unsafe_row = watcher.first_issue()
                    timed_out = budget is not None and time.monotonic() - began > budget
                    if (self.stop_requested or timed_out or unsafe_row) and signalled_at is None:
                        watchdog_expired = timed_out
                        child.send_signal(signal.SIGTERM)  # exact owned, unreaped Popen child
                        signalled_at = time.monotonic()
                        reason = 'unsafe result row' if unsafe_row else 'controller watchdog' if timed_out else 'requested signal'
                        self.save(status='stopping', stop_reason=reason, unsafe_row=unsafe_row)
                    if signalled_at is not None and time.monotonic() - signalled_at > self.args.long_timeout + 90:
                        self.save(status='blocked', child_may_still_be_running=True,
                                  stop_reason='Runner did not finish bounded graceful cleanup; inspect owned child on Mac')
                        raise Halt('Runner cleanup exceeded its bounded window; no subsequent browser launched')
                    try:
                        child.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        pass
            except BaseException:
                # A controller error must not leave an unrecorded active runner
                # or launch another browser. Never kill unrelated process groups.
                if child.poll() is None and not self.state.get('child_may_still_be_running'):
                    child.send_signal(signal.SIGTERM)
                    try:
                        child.wait(timeout=self.args.long_timeout + 90)
                    except subprocess.TimeoutExpired:
                        self.state.update(child_pid=child.pid, child_may_still_be_running=True)
                raise
            finally:
                if child.poll() is not None:
                    self.child = None
        self.save(child_pid=None, last_exit_code=child.returncode, last_log=str(log_path))
        if child.returncode != 0:
            raise Halt('Stage exited ' + str(child.returncode) + '; inspect ' + str(log_path))
        if watchdog_expired:
            raise Halt('Controller watchdog expired; runner was stopped for inspection')
        if unsafe_row:
            raise Halt('Unsafe result stopped the runner: ' + json.dumps(unsafe_row))
        assert_files_unchanged(self.cohort['files'])
        return not self.stop_requested

    def run_engine(self, unit, engine):
        directory = self.args.output / unit['name'] / engine
        binary_key = 'native_snapshot' if engine == 'lumen' else 'chromium_binary'
        binary = self.cohort['files'][binary_key]
        if (directory / 'run.json').exists():
            previous = inspect_run(self.runner, directory, unit, engine, binary['sha256'], repair=True,
                                   allow_known_reference_teardown=getattr(self.args, 'allow_known_reference_teardown', False))
            if previous['issues']:
                raise Halt('Unsafe prior run cannot be resumed automatically: ' + str(directory))
            if previous['complete']:
                if engine == 'chromium':
                    self.pin_chromium_version(previous)
                self.state.setdefault('units', {}).setdefault(unit['name'], {})[engine] = previous
                self.save()
                return previous
        args = self.args
        command = [sys.executable, str(args.runner), 'run', '--inventory', str(args.inventory),
                   '--engine', engine, '--binary', binary['path'], '--output', str(directory),
                   '--types', unit['type'], '--offset', str(unit['offset']), '--limit', str(unit['limit']),
                   '--origin', args.origin, '--secure-origin', args.secure_origin, '--h2-origin', args.h2_origin,
                   '--timeout', str(args.timeout), '--long-timeout', str(args.long_timeout),
                   '--server-config', str(args.server_config), '--fixture-manifest', str(args.fixture_manifest)]
        if unit['phase'] == 'smoke':
            command.append('--strict-infrastructure')
        for prefix in unit['prefixes']:
            command.extend(['--prefix', prefix])
        if (directory / 'run.json').exists():
            command.append('--resume')
        if args.max_seconds is not None:
            command.extend(['--max-seconds', str(args.max_seconds)])
        if engine == 'chromium':
            command.extend('--chromium-arg=' + value for value in self.cohort['configuration']['chromium_args'])
        stage_budget = (args.max_seconds if args.max_seconds else unit['selected_files'] * (args.long_timeout + 45)) + args.long_timeout + 120
        watcher = ResultWatch(directory / 'results.jsonl', smoke=unit['phase'] == 'smoke')
        self.launch(command, self.args.output / unit['name'] / (engine + '.runner.log'), unit['name'], engine, stage_budget, watcher)
        result = inspect_run(self.runner, directory, unit, engine, binary['sha256'],
                             allow_known_reference_teardown=getattr(self.args, 'allow_known_reference_teardown', False))
        if engine == 'chromium':
            self.pin_chromium_version(result)
        self.state.setdefault('units', {}).setdefault(unit['name'], {})[engine] = result
        self.save()
        if result['issues']:
            raise Halt('Unsafe run stopped this cohort: ' + str(directory))
        return result

    def run(self):
        broad_finished = 0
        for unit in self.cohort['units']:
            previous = self.state.get('units', {}).get(unit['name'], {})
            if previous.get('paired_complete'):
                comparison = Path(previous['comparison'])
                if not comparison.is_file() or file_digest(comparison) != previous.get('comparison_sha256'):
                    raise Halt('Completed pair comparison changed or is missing: ' + unit['name'])
                continue
            if self.stop_requested:
                raise Halt('Stop requested between paired shards')
            if unit['phase'] == 'broad' and self.args.max_shards is not None and broad_finished >= self.args.max_shards:
                self.save(status='paused', stop_reason='Invocation shard limit reached')
                return
            free_gib = shutil.disk_usage(self.args.output).free / (1024 ** 3)
            self.save(disk_free_gib=round(free_gib, 3), minimum_free_gib=self.args.min_free_gib)
            if free_gib < self.args.min_free_gib:
                raise Pause('Disk headroom below configured minimum before ' + unit['name'])
            lumen = self.run_engine(unit, 'lumen')
            if self.stop_requested:
                self.save(status='paused', stop_reason='Signal after native cleanup; paired shard remains incomplete')
                return
            chromium = self.run_engine(unit, 'chromium')
            comparison = self.args.output / unit['name'] / 'comparison.json'
            command = [sys.executable, str(self.args.runner), 'compare',
                       '--lumen', str(comparison.parent / 'lumen'), '--chromium', str(comparison.parent / 'chromium'),
                       '--output', str(comparison)]
            if not self.stop_requested:
                self.launch(command, comparison.parent / 'compare.log', unit['name'], 'compare', 120)
            if comparison.is_file():
                qualify_comparison(comparison, self.cohort, chromium)
            complete = lumen['complete'] and chromium['complete'] and comparison.is_file()
            self.state.setdefault('units', {}).setdefault(unit['name'], {}).update(
                paired_complete=complete, comparison=str(comparison) if comparison.is_file() else None,
                comparison_sha256=file_digest(comparison) if comparison.is_file() else None)
            self.save()
            aggregate(self.runner, self.args.output, self.cohort, self.state)
            if not complete:
                self.save(status='paused', stop_reason='Paired shard incomplete; resume the same cohort before advancing')
                return
            if unit['phase'] == 'broad':
                broad_finished += 1
        self.save(status='complete', stop_reason=None, current_unit=None, current_stage=None)


def aggregate(runner, output, cohort, state):
    """Recompute scalar counters from completed broad shards; smoke is excluded."""
    groups = {}
    for unit in cohort['units']:
        if unit['phase'] != 'broad':
            continue
        group = groups.setdefault(unit['type'], {'planned_files': 0, 'paired_files': 0,
                                                 'lumen': Counter(), 'chromium': Counter(), 'parity': Counter(),
                                                 'lumen_reporting': Counter(), 'chromium_reporting': Counter()})
        group['planned_files'] += unit['selected_files']
        progress = state.get('units', {}).get(unit['name'], {})
        if not progress.get('paired_complete'):
            continue
        comparison_path = Path(progress['comparison'])
        if file_digest(comparison_path) != progress.get('comparison_sha256'):
            raise ValueError('Completed comparison changed; refuse aggregate reuse')
        report = json.loads(comparison_path.read_text())
        group['paired_files'] += unit['selected_files']
        for engine in ('lumen', 'chromium'):
            summary = report[engine]
            group[engine].update({key: summary[key] for key in
                                 ('attempted_files', 'files_passing_all_assertions', 'executed_subtests_excluding_notrun')})
            group[engine]['passing_subtests'] += summary['subtest_outcomes'].get('PASS', 0)
            group[engine + '_reporting'].update(progress.get(engine, {}).get('reporting_counts', {}))
        group['parity'].update(report['parity']['counts'])
    result = {}
    for kind, group in groups.items():
        value = {'planned_files': group['planned_files'], 'paired_completed_files': group['paired_files'],
                 'not_yet_paired_files': group['planned_files'] - group['paired_files']}
        for engine in ('lumen', 'chromium'):
            counts = group[engine]
            value[engine] = dict(counts)
            value[engine]['attempted_file_pass_rate'] = counts['files_passing_all_assertions'] / counts['attempted_files'] if counts['attempted_files'] else None
            value[engine]['subtest_pass_rate'] = counts['passing_subtests'] / counts['executed_subtests_excluding_notrun'] if counts['executed_subtests_excluding_notrun'] else None
            value[engine]['reporting_counts'] = dict(group[engine + '_reporting'])
        counts = group['parity']
        value['parity_counts'] = dict(counts)
        value['chromium_pass_retention'] = counts['also_passed_by_lumen'] / counts['chromium_passing_assertions'] if counts['chromium_passing_assertions'] else None
        result[kind] = value
    incidents, incident_count = [], 0
    for unit in cohort['units']:
        incident = state.get('units', {}).get(unit['name'], {}).get('chromium', {}).get('functional_reference_incident')
        if incident:
            incident_count += 1
            if len(incidents) < 20:
                incidents.append({'unit': unit['name'], 'phase': unit['phase'], 'type': unit['type'], 'incident': incident})
    functional_only = cohort.get('configuration', {}).get('allow_known_reference_teardown', False)
    write_json(output / 'aggregate.json', {
        'cohort_sha256': digest(cohort), 'by_type': result, 'inventory_coverage': cohort['inventory_coverage'],
        'fixture_manifest': cohort['files']['fixture_manifest'],
        'known_fixture_limitations': cohort.get('known_fixture_limitations'),
        'reference_qualification': {'policy': REFERENCE_POLICY if functional_only else 'strict_default',
                                    'functional_only': functional_only,
                                    'reference_performance_release_gate_passed': False if functional_only else None,
                                    'observed_teardown_incident_count': incident_count, 'first_20_incidents': incidents,
                                    'complete_incident_evidence': 'Per-unit chromium state in controller.json and each comparison.json'},
        'note': 'Only completed matched broad shards contribute. Smoke is excluded; incomplete shards retain their separate comparison. '
                'Report testharness and test262 separately. No combined browser-quality score or full-suite parity claim is inferred.'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--runner', type=Path, default=Path('tests/wpt/full_runner.py'))
    parser.add_argument('--inventory', type=Path, default=Path('output/wpt/inventory.json'))
    parser.add_argument('--native-binary', type=Path, default=Path('build/lumen'))
    parser.add_argument('--chromium-binary', type=Path, default=Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'))
    parser.add_argument('--server-config', type=Path, default=Path('output/wpt/server.config.json'))
    parser.add_argument('--fixture-manifest', type=Path, required=True)
    parser.add_argument('--ca-file', type=Path, default=Path('output/wpt/upstream/tools/certs/cacert.pem'))
    parser.add_argument('--host-map', default=os.environ.get('LUMEN_HOST_MAP'))
    parser.add_argument('--host-map-mode', choices=['exact', 'suffix'], default='exact')
    parser.add_argument('--chromium-host-rules', required=True, help='Exact validated WPT resolver rules, including nonexistent-host behavior')
    parser.add_argument('--chromium-spki', default=SPKI)
    parser.add_argument('--chromium-arg', action='append', default=[])
    parser.add_argument('--allow-known-reference-teardown', action='store_true',
                        help='Explicit functional-only CfT156 policy; retain its exact known post-report watchdog failure')
    parser.add_argument('--dependency', type=Path, action='append', default=[], help='Additional native/Chrome runtime file to bind and guard')
    parser.add_argument('--output', type=Path, required=True, help='Fresh versioned cohort directory; use --resume only for the same inputs')
    parser.add_argument('--types', default='testharness,test262')
    parser.add_argument('--prefix', action='append', default=[])
    parser.add_argument('--offset', type=int, default=0, help='Offset within each requested class after prefix filtering')
    parser.add_argument('--limit', type=int, help='Maximum broad variants per requested class, before sharding')
    parser.add_argument('--shard-size', type=int, default=250)
    parser.add_argument('--smoke-count', type=int, default=6)
    parser.add_argument('--max-seconds', type=float, help='Per-engine invocation budget; a partial pair pauses for explicit resume')
    parser.add_argument('--max-shards', type=int, help='Maximum newly completed broad pairs this invocation')
    parser.add_argument('--min-free-gib', type=float, default=3, help='Pause between paired shards below this disk headroom')
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--long-timeout', type=float, default=65)
    parser.add_argument('--origin', default='http://web-platform.test:8000')
    parser.add_argument('--secure-origin', default='https://web-platform.test:18443')
    parser.add_argument('--h2-origin', default='https://web-platform.test:9000')
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--prepare-only', action='store_true', help='Create the immutable cohort and plan, without browser execution')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Execute only on the authorized Mac host through devinmac3d')
    if not args.host_map:
        parser.error('Supply the validated native WPT host map')
    if args.max_seconds is not None and args.max_seconds <= 0 or args.max_shards is not None and args.max_shards <= 0:
        parser.error('Optional execution budgets must be positive')
    if args.timeout <= 0 or args.long_timeout <= 0:
        parser.error('Timeouts must be positive')
    if args.min_free_gib < 0:
        parser.error('Minimum disk headroom must be nonnegative')
    args.repo = args.repo.resolve(strict=True)
    for key in ('runner', 'inventory', 'native_binary', 'chromium_binary', 'server_config', 'fixture_manifest', 'ca_file', 'output'):
        path = getattr(args, key)
        setattr(args, key, (args.repo / path).resolve())
    args.dependency = [(args.repo / path).resolve() for path in args.dependency]
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / 'controller.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        runner = load_runner(args.runner)
        if args.allow_known_reference_teardown:
            load_reference_policy()
        inventory = json.loads(args.inventory.read_text())
        fixture = json.loads(args.fixture_manifest.read_text())
        fixture_identity = validate_fixture(fixture, inventory, args)
        units, coverage = plan_units(runner, inventory, args.types.split(','), args.prefix,
                                     args.offset, args.limit, args.shard_size, args.smoke_count)
        chromium_args = ['--host-resolver-rules=' + args.chromium_host_rules,
                         '--ignore-certificate-errors-spki-list=' + args.chromium_spki, *args.chromium_arg]
        configuration = {'repo': str(args.repo), 'types_in_execution_order': args.types.split(','),
                         'prefixes': args.prefix, 'offset_per_type': args.offset, 'limit_per_type': args.limit,
                         'shard_size': args.shard_size, 'smoke_count': args.smoke_count,
                         'origin': args.origin, 'secure_origin': args.secure_origin, 'h2_origin': args.h2_origin,
                         'timeout': args.timeout, 'long_timeout': args.long_timeout,
                         'host_map': args.host_map, 'host_map_mode': args.host_map_mode,
                         'chromium_args': chromium_args,
                         'allow_known_reference_teardown': args.allow_known_reference_teardown,
                         'reference_policy': REFERENCE_POLICY if args.allow_known_reference_teardown else 'strict_default',
                         'reporter_policy': {'smoke': 'strict attached and complete nonempty assertions',
                                             'broad': 'TIMEOUT/CRASH/ERROR may lack reporter; attempted failures remain in rates; first unsafe committed row stops'},
                         'inherited_runtime_environment_sha256': environment_signature(execution_environment(args))}
        cohort_path = args.output / 'cohort.json'
        if cohort_path.exists():
            if not args.resume:
                raise ValueError('Cohort exists; use --resume or a fresh generation')
            cohort = json.loads(cohort_path.read_text())
            if cohort['configuration'] != configuration or cohort['units'] != units:
                raise ValueError('Requested scope/fixtures differ; create a new generation')
            if cohort['fixture_identity'] != fixture_identity:
                raise ValueError('Verified server identity differs from cohort')
            for key in ('runner', 'inventory', 'chromium_binary', 'server_config', 'fixture_manifest', 'ca_file'):
                if str(getattr(args, key)) != cohort['files'][key]['path']:
                    raise ValueError('Input path differs from cohort: ' + key)
            if str(Path(__file__).resolve()) != cohort['files']['controller']['path']:
                raise ValueError('Controller path differs from cohort')
            assert_files_unchanged(cohort['files'], content=True)
            state = json.loads((args.output / 'controller.json').read_text())
            if state.get('cohort_sha256') != digest(cohort):
                raise ValueError('Cohort manifest changed after preparation')
            if state.get('status') == 'blocked' or state.get('child_may_still_be_running') or state.get('child_pid') is not None:
                raise ValueError('Prior cohort is blocked; inspect its recorded owned child before creating a new generation')
        else:
            if args.resume:
                raise ValueError('No existing cohort to resume')
            unexpected = [path for path in args.output.iterdir() if path.name != 'controller.lock']
            if unexpected:
                raise ValueError('Output is not fresh; no existing files are overwritten')
            snapshot = snapshot_native(args.native_binary)
            files = {key: record_file(getattr(args, key)) for key in
                     ('runner', 'inventory', 'chromium_binary', 'server_config', 'fixture_manifest', 'ca_file')}
            files['native_snapshot'] = snapshot['snapshot']
            files['reporter'] = record_file(args.runner.with_name('testharnessreport.js'))
            files['monitor'] = record_file(args.repo / 'tools/monitor_process.py')
            files['controller'] = record_file(Path(__file__))
            if args.allow_known_reference_teardown:
                files['reference_policy'] = record_file(Path(__file__).with_name('run_cft156_url77.py'))
            for name in ('icudtl.dat', 'cacert.pem'):
                resource = args.native_binary.parent / name
                if resource.is_file():
                    files['native_adjacent_' + name] = record_file(resource)
            for index, path in enumerate(args.dependency):
                files['dependency_' + str(index)] = record_file(path)
            cohort = {'schema': 1, 'created_unix_ms': round(time.time() * 1000), 'configuration': configuration,
                      'native_snapshot': snapshot, 'files': files, 'units': units,
                      'fixture_identity': fixture_identity,
                      'inventory_coverage': coverage, 'known_fixture_limitations': fixture.get('known_limitations'),
                      'scope_note': 'Only macOS. No iPhone/iPad or physical-device result is implied.'}
            write_json(cohort_path, cohort)
            state = {'status': 'prepared', 'units': {}, 'cohort_sha256': digest(cohort)}
        controller = Controller(args, runner, cohort, state)
        controller.save()
        aggregate(runner, args.output, cohort, state)
        if args.prepare_only:
            print(json.dumps({'status': 'prepared', 'cohort': str(cohort_path), 'native_snapshot': cohort['files']['native_snapshot']['path'], 'paired_units': len(units)}))
            return
        previous_handlers = {sig: signal.signal(sig, controller.request_stop) for sig in (signal.SIGINT, signal.SIGTERM)}
        try:
            controller.run()
        except Halt as exc:
            controller.diagnostic_comparison()
            if controller.state.get('status') != 'blocked':
                controller.save(status='paused' if controller.stop_requested or isinstance(exc, Pause) else 'blocked', stop_reason=str(exc))
            print(str(exc), file=sys.stderr)
            raise SystemExit(2)
        except Exception as exc:
            controller.diagnostic_comparison()
            controller.save(status='blocked', stop_reason=type(exc).__name__ + ': ' + str(exc))
            raise
        finally:
            for sig, handler in previous_handlers.items():
                signal.signal(sig, handler)
        print(json.dumps({'status': controller.state['status'], 'controller': str(args.output / 'controller.json'), 'aggregate': str(args.output / 'aggregate.json')}))


if __name__ == '__main__':
    main()

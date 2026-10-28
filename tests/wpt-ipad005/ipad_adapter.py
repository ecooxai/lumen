#!/usr/bin/env python3
"""WPT execution adapter for the explicitly authorized booted iPad Simulator.

No simulator is booted, shut down, erased or selected implicitly. The installed
app must match the requested executable. The official WPT server supplies the
synchronous reporter; results use full_runner's Collector and Sampler. Device
IPA execution is not established by this adapter. This generation uses explicit
simulator platform termination after a case. It does not claim a natural or
graceful application exit, and does not reclassify earlier SIGTERM failures.
"""
import importlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import tempfile
import time


AUTHORIZED_DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
AUTHORIZED_BUNDLE = 'com.ecooxai.lumen'
CLEANUP_POLICY = 'exact-ipad-simctl-platform-termination-v1'


def select_device(listing, device):
    if device.upper() != AUTHORIZED_DEVICE:
        raise ValueError('Only the explicitly authorized iPad simulator may be tested')
    for runtime, devices in listing.get('devices', {}).items():
        for entry in devices:
            if entry.get('udid', '').upper() != device.upper():
                continue
            if 'ipad' not in entry.get('deviceTypeIdentifier', '').lower():
                raise ValueError('The selected device is not identified as an iPad')
            if entry.get('state') != 'Booted' or entry.get('isAvailable') is False:
                raise ValueError('The authorized iPad must already be booted and available')
            return {'udid': entry['udid'], 'name': entry.get('name'),
                    'device_type': entry['deviceTypeIdentifier'], 'runtime': runtime,
                    'state': entry['state']}
    raise ValueError('The authorized iPad was not found in simulator inventory')


def validate_bundle_id(value):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', value):
        raise ValueError('Invalid application bundle identifier')
    return value


def launch_pid(output, bundle_id):
    matches = re.findall(r'^' + re.escape(bundle_id) + r':\s*([1-9][0-9]*)\s*$', output, re.M)
    if len(matches) != 1:
        raise ValueError('simctl did not return exactly one PID for the requested bundle')
    return int(matches[0])


def launch_arguments(device, bundle_id, stdout_path, stderr_path, url):
    if device.upper() != AUTHORIZED_DEVICE:
        raise ValueError('Unapproved simulator target')
    return ['launch', '--stdout=' + str(stdout_path), '--stderr=' + str(stderr_path),
            device, validate_bundle_id(bundle_id), url]


def launch_environment(base, profile, certificate=None):
    """Forward only explicit test settings to the app, never inherited child flags."""
    env = {key: value for key, value in base.items() if not key.startswith('SIMCTL_CHILD_')}
    for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
        env.pop(key, None)
    child = {'LUMEN_CONSOLE': '1',
             'LUMEN_PROFILE_DIR': str(profile), 'LUMEN_DOWNLOAD_DIR': str(profile / 'downloads')}
    if base.get('LUMEN_HOST_MAP'):
        child['LUMEN_HOST_MAP'] = base['LUMEN_HOST_MAP']
    if base.get('LUMEN_HOST_MAP_MODE'):
        child['LUMEN_HOST_MAP_MODE'] = base['LUMEN_HOST_MAP_MODE']
    if certificate is not None:
        child['SSL_CERT_FILE'] = str(certificate)
    env.update({'SIMCTL_CHILD_' + key: value for key, value in child.items()})
    return env


def parse_process_identity(raw, pid):
    for line in raw.splitlines():
        parts = line.split(None, 6)
        if len(parts) == 7 and parts[0] == str(pid):
            return {'pid': pid, 'start': ' '.join(parts[1:6]), 'executable': parts[6].strip()}
    return None


def process_identity(pid):
    result = subprocess.run(['ps', '-ww', '-p', str(pid), '-o', 'pid=,lstart=,comm='],
                            capture_output=True, text=True, timeout=5)
    if result.returncode not in (0, 1):
        raise RuntimeError('Cannot inspect the launched app process identity')
    return parse_process_identity(result.stdout, pid)


def process_exists(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


class IpadAdapter:
    def __init__(self, device, bundle_id, runtime=None):
        if platform.system() != 'Darwin':
            raise ValueError('iPad execution is restricted to the authorized Mac host')
        if device.upper() != AUTHORIZED_DEVICE:
            raise ValueError('Only the explicitly authorized iPad simulator may be tested')
        self.device, self.bundle_id = device.upper(), validate_bundle_id(bundle_id)
        if self.bundle_id != AUTHORIZED_BUNDLE:
            raise ValueError('This task authorizes only the identified Lumen app bundle')
        self.runtime = runtime or importlib.import_module('full_runner')
        target = select_device(json.loads(self._checked(['list', 'devices', '--json'])), self.device)
        self.app = Path(self._checked(['get_app_container', self.device, self.bundle_id, 'app']).strip()).resolve()
        self.container = Path(self._checked(['get_app_container', self.device, self.bundle_id, 'data']).strip()).resolve()
        info = plistlib.loads((self.app / 'Info.plist').read_bytes())
        if info.get('CFBundleIdentifier') != self.bundle_id:
            raise ValueError('Installed app bundle identifier does not match')
        executable = info.get('CFBundleExecutable')
        if not isinstance(executable, str) or Path(executable).name != executable or executable in ('', '.', '..'):
            raise ValueError('Invalid installed app executable name')
        supported = info.get('CFBundleSupportedPlatforms', [])
        if supported and 'iPhoneSimulator' not in supported:
            raise ValueError('Installed bundle does not declare the iOS Simulator platform')
        self.binary = self.app / executable
        if not self.binary.is_file() or not self.container.is_dir():
            raise ValueError('Installed app executable or data container is missing')
        self.temp_root = self.container / 'tmp'
        self.temp_root.mkdir(exist_ok=True)
        self.capture_next = False
        self.metadata = {'execution_class': 'iPad Simulator; separate simulator build, not a device IPA',
                         'device': target, 'bundle_id': self.bundle_id,
                         'bundle_version': info.get('CFBundleVersion'),
                         'minimum_os_version': info.get('MinimumOSVersion'),
                         'supported_platforms': supported,
                         'installed_app': str(self.app), 'installed_binary': str(self.binary),
                         'installed_binary_sha256': self.runtime.file_digest(self.binary),
                         'adapter_sha256': self.runtime.file_digest(Path(__file__)),
                         'host_map': os.environ.get('LUMEN_HOST_MAP'),
                         'host_map_mode': os.environ.get('LUMEN_HOST_MAP_MODE'),
                         'ca_sha256': self.runtime.file_digest(os.environ['SSL_CERT_FILE']) if os.environ.get('SSL_CERT_FILE') else None,
                         'reporter_delivery': 'official WPT /resources/testharnessreport.js exact file alias; no prefile injection',
                         'profile_isolation': 'fresh owned directory in app data container per test',
                         'resource_scope': 'app process tree on the Mac; shared CoreSimulator and WindowServer overhead excluded; not physical iPad performance',
                         'process_identity': 'host PID, ps lstart with one-second resolution, executable path',
                         'cleanup_policy': CLEANUP_POLICY,
                         'cleanup_scope': 'Explicit simctl terminate for the exact device/bundle, only after sole launched PID/start/path and registration checks; the check and platform request are not atomic',
                         'app_graceful_shutdown_verified': False,
                         'app_exit_status_available': False,
                         'resource_lifetime': 'App process sampling continues through platform teardown; the termination CLI has a separate process-tree stream'}

    @staticmethod
    def _checked(arguments, timeout=20, env=None):
        result = subprocess.run(['xcrun', 'simctl', *arguments], capture_output=True,
                                text=True, timeout=timeout, env=env)
        if result.returncode:
            raise RuntimeError('simctl ' + arguments[0] + ' failed: ' + result.stderr.strip()[:2000])
        return result.stdout

    def _verify_binary(self, binary):
        expected = self.metadata['installed_binary_sha256']
        if self.runtime.file_digest(binary) != expected or self.runtime.file_digest(self.binary) != expected:
            raise ValueError('Installed iPad app differs from the requested test executable; install the matching build first')

    def _running_instances(self):
        raw = subprocess.check_output(['ps', '-A', '-ww', '-o', 'pid=,comm='], text=True, timeout=5)
        pids = []
        wanted = self.binary.resolve()
        for line in raw.splitlines():
            parts = line.split(None, 1)
            if len(parts) == 2 and parts[0].isdigit() and Path(parts[1].strip()).resolve() == wanted:
                pids.append(int(parts[0]))
        return pids

    def _platform_terminate(self, prefix):
        """Observe one bounded owned CLI; preserve every request/cleanup failure."""
        self.runtime.monitor_module()  # Resolve dependencies before launching.
        command = ['xcrun', 'simctl', 'terminate', self.device, self.bundle_id]
        log_path = prefix.with_suffix('.terminate.log')
        evidence_path = prefix.with_suffix('.terminate.json')
        record = {'command': command, 'timeout_s': 20, 'log': str(log_path),
                  'natural_exit_code': None, 'timed_out': False, 'errors': []}
        process, sampler = None, None
        with log_path.open('xb') as log:
            try:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                record['pid'] = process.pid
                sampler = self.runtime.Sampler(process.pid, prefix.with_suffix('.terminate.metrics.jsonl'))
                try:
                    record['natural_exit_code'] = process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    record['timed_out'] = True
                    record['errors'].append('Platform termination CLI exceeded its 20-second deadline')
            except Exception as error:
                record['errors'].append(type(error).__name__ + ': ' + str(error))
            finally:
                if process is not None:
                    try:
                        record['process_cleanup'] = self.runtime.terminate_owned(process, sampler)
                    except Exception as error:
                        record['process_cleanup'] = {'process_group_gone': False,
                                                     'error': type(error).__name__ + ': ' + str(error)}
                if sampler is not None:
                    try:
                        record['metrics'] = sampler.stop()
                    except Exception as error:
                        record['metrics'] = {'samples': 0, 'telemetry_status': 'failed',
                                             'errors': [type(error).__name__ + ': ' + str(error)]}
                else:
                    record['metrics'] = {'samples': 0, 'telemetry_status': 'unavailable',
                                         'unavailable_reason': 'No CLI sampler was attached', 'errors': []}
        cleanup = record.get('process_cleanup') or {}
        if record['natural_exit_code'] != 0:
            record['errors'].append('Platform termination did not return natural exit code 0')
        if (cleanup.get('error') or cleanup.get('process_group_gone') is not True
                or cleanup.get('sent_sigterm') is not False or cleanup.get('sent_sigkill') is not False):
            record['errors'].append('Termination CLI did not exit with a verified empty owned group without signals')
        if self.runtime.measurement_failure({'metrics': record['metrics']}):
            record['errors'].append('Termination CLI resource collector failed')
        record['request_passed'] = not record['errors']
        record['metrics_note'] = 'Short or empty samples do not qualify sustained CLI performance.'
        self.runtime.write_json(evidence_path, record)
        return record

    def _stop_owned_app(self, identity, prefix):
        """Use the platform lifecycle contract, never a failed-signal fallback.

        The same-device lock is held by ipad_runner. Current app registration,
        sole app process and PID/start/path are checked immediately before the
        request. A concurrent external launch cannot be excluded atomically, so
        any observed replacement remains unsafe and is never terminated here.
        """
        result = {'stopped': False, 'cleanup_policy': CLEANUP_POLICY,
                  'platform_termination_requested': False, 'sent_sigterm': False,
                  'sent_sigkill': False, 'app_graceful_shutdown_verified': False,
                  'natural_app_exit_code': None}
        current, running = process_identity(identity['pid']), self._running_instances()
        result['before'] = {'identity': current, 'matching_installed_app_pids': running}
        if current != identity:
            if running:
                result['error'] = 'Original app exited but another app instance is present; no termination requested'
            else:
                result.update(stopped=True, reason='original_app_already_gone_no_platform_request', app_gone=True)
            return result
        if running != [identity['pid']]:
            result['error'] = 'Sole owned app identity is not established; no bundle termination requested'
            return result
        try:
            select_device(json.loads(self._checked(['list', 'devices', '--json'], timeout=5)), self.device)
            registered = Path(self._checked(['get_app_container', self.device, self.bundle_id, 'app'], timeout=5).strip()).resolve()
            if registered != self.app.resolve():
                raise RuntimeError('Installed app registration changed before platform termination')
            if process_identity(identity['pid']) != identity or self._running_instances() != [identity['pid']]:
                raise RuntimeError('App ownership changed immediately before platform termination')
            result['platform_termination_requested'] = True
            result['request'] = self._platform_terminate(prefix)
            began = time.monotonic()
            deadline = began + 10
            while True:
                current, running = process_identity(identity['pid']), self._running_instances()
                if current != identity and not running or time.monotonic() >= deadline:
                    break
                time.sleep(.1)
            result['after'] = {'identity': current, 'matching_installed_app_pids': running,
                               'verification_elapsed_s': round(time.monotonic() - began, 3)}
            result['app_gone'] = current != identity and not running
            if not result['request']['request_passed']:
                result['error'] = 'Platform termination request or its owned CLI cleanup/telemetry failed'
            elif not result['app_gone']:
                result['error'] = 'Owned app or a replacement remained after platform termination'
            else:
                result.update(stopped=True, reason='platform_termination_complete')
        except Exception as error:
            result['error'] = type(error).__name__ + ': ' + str(error)
        return result

    def _remove_owned_profile(self, profile):
        if profile.parent != self.temp_root or not profile.name.startswith('lumen-wpt-') or profile.is_symlink():
            raise ValueError('Refusing to remove an unrecognized profile directory')
        shutil.rmtree(profile)

    def run(self, binary, url, timeout, prefix, reporter=None):
        if reporter is not None:
            raise ValueError('The iPad WPT adapter requires the synchronous server reporter, not prefile injection')
        self._verify_binary(binary)
        if timeout <= 0:
            raise ValueError('Test watchdog must be positive')
        if self._running_instances():
            raise RuntimeError('Lumen is already running on this iPad; stop that specific instance before the isolated test run')
        collector = self.runtime.Collector()
        prefix = Path(prefix)
        prefix.parent.mkdir(parents=True, exist_ok=True)
        profile = Path(tempfile.mkdtemp(prefix='lumen-wpt-', dir=self.temp_root))
        try:
            (profile / 'downloads').mkdir()
            certificate = None
            if os.environ.get('SSL_CERT_FILE'):
                certificate = profile / 'wpt-ca.pem'
                shutil.copyfile(os.environ['SSL_CERT_FILE'], certificate)
            env = launch_environment(os.environ, profile, certificate)
            stdout_path = prefix.with_suffix('.stdout.log').resolve()
            stderr_path = prefix.with_suffix('.stderr.log').resolve()
            stdout_path.write_bytes(b'')
            stderr_path.write_bytes(b'')
        except Exception:
            self._remove_owned_profile(profile)
            raise
        began = time.monotonic()
        identity, sampler, pid = None, None, None
        launch_attempted = False
        result = None
        cleanup = {'stopped': True, 'reason': 'app_not_launched'}
        metrics = None
        capture = self.capture_next
        self.capture_next = False
        try:
            launch_attempted = True
            launched = self._checked(launch_arguments(self.device, self.bundle_id, stdout_path, stderr_path, url),
                                     timeout=min(15, timeout), env=env)
            pid = launch_pid(launched, self.bundle_id)
            identity = process_identity(pid)
            if identity is not None and Path(identity['executable']).resolve() != self.binary.resolve():
                identity = None
                raise RuntimeError('Returned simulator app PID does not identify the installed Lumen executable')
            if identity is not None:
                sampler = self.runtime.Sampler(pid, prefix.with_suffix('.metrics.jsonl'))
            with stdout_path.open(errors='replace') as out, stderr_path.open(errors='replace') as err, prefix.with_suffix('.console.log').open('w') as merged:
                readers = [('stdout', out), ('stderr', err)]
                carries = {'stdout': '', 'stderr': ''}
                exited = False
                while True:
                    for source, reader in readers:
                        carries[source] += reader.read()
                        lines = carries[source].split('\n')
                        carries[source] = lines.pop()
                        for line in lines:
                            merged.write('[' + source + '] ' + line + '\n')
                            collector.line(line)
                    merged.flush()
                    exited = not process_exists(pid)
                    if collector.done or exited or time.monotonic() - began >= timeout:
                        for source in carries:
                            if carries[source]:
                                collector.line(carries[source])
                        break
                    time.sleep(.025)
            fallback = {'status': 'CRASH' if exited else 'TIMEOUT',
                        'message': 'App exited before harness completion; exit status is unavailable from detached simctl launch' if exited else 'External file watchdog expired'}
            result = collector.result(fallback)
            result.update(pid=pid, device=self.device, natural_exit_code=None)
            if capture and identity is not None and process_identity(pid) == identity:
                image = prefix.with_suffix('.ipad.png').resolve()
                try:
                    self._checked(['io', self.device, 'screenshot', str(image)], timeout=10)
                    result['screenshot'] = str(image)
                except Exception as exc:
                    result['screenshot_error'] = str(exc)
        except Exception as exc:
            result = collector.result({'status': 'INFRA_ERROR', 'message': type(exc).__name__ + ': ' + str(exc)})
            result.update(pid=pid, device=self.device)
        finally:
            if identity is not None:
                try:
                    cleanup = self._stop_owned_app(identity, prefix)
                except Exception as exc:
                    cleanup = {'stopped': False, 'error': str(exc)}
            elif launch_attempted:
                try:
                    remaining = self._running_instances()
                    cleanup = {'stopped': not remaining, 'reason': 'no_verified_launch_identity',
                               'unverified_app_pids': remaining}
                    if remaining:
                        cleanup['error'] = 'No verified launched-process identity; no app was terminated'
                except Exception as exc:
                    cleanup = {'stopped': False, 'error': 'Unable to establish post-launch app state: ' + str(exc)}
            # Keep the app stream alive until platform teardown is observed.
            # Its samples never include the separately observed simctl CLI.
            if sampler is not None:
                try:
                    metrics = sampler.stop()
                except Exception as exc:
                    metrics = {'errors': [str(exc)], 'samples': 0, 'telemetry_status': 'failed'}
            if cleanup.get('stopped'):
                try:
                    self._remove_owned_profile(profile)
                except Exception as exc:
                    cleanup['profile_error'] = str(exc)
            else:
                cleanup['retained_profile'] = str(profile)
        result.update(duration_ms=round((time.monotonic() - began) * 1000),
                      metrics=metrics or {'samples': 0, 'telemetry_status': 'unavailable',
                                          'errors': [],
                                          'unavailable_reason': 'App exited before sampler attachment or no verified app PID was available'},
                      cleanup=cleanup, launch_identity=identity)
        if not cleanup.get('stopped'):
            prefix.with_suffix('.cleanup-error.txt').write_text(json.dumps(cleanup, indent=2) + '\n')
        return result

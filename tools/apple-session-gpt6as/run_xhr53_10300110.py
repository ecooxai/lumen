#!/usr/bin/env python3
"""Replay the unchanged supplemental XHR53 on a frozen actual Mac artifact.

This reuses the existing pinned source closure, ports, reporter, watchdogs and
native GUI collector. Its denominator never joins the original1515 cohort.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import http.client
import importlib.util
import json
import os
from pathlib import Path
import platform
import socket
import ssl
import subprocess
import time


PIN = '7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
SCHEDULE = '523450d693819e78c75b69828318075cd493928ab91d62c4a0718238559623ce'
CLOSURE = '2973a54d2a187f72b45188a2f4e75ed572b05a6135483744159998ea6d8ad612'
CHROME = '230f0db8c5086182f46c59c867c0d461eb525d6552dfad226ea6b4d9ec249db2'
utc = lambda: datetime.now(timezone.utc).isoformat()


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def save(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)
    if read(path) != value:
        raise RuntimeError('Atomic evidence readback failed: ' + str(path))


def load(name, path):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-seconds', type=float, default=240)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        raise RuntimeError('This replay requires the actual native Mac GUI')
    root = Path.cwd().resolve()
    out = args.output.resolve()
    out.relative_to(root / 'out')
    out.mkdir(parents=True, exist_ok=False)
    fixture = root / 'out/10292019r-mac/xhr-fixture-004'
    upstream = fixture / 'upstream'
    evidence = root / 'agent/evidence/10.30-0110-xhr53'
    build = read(args.build_summary)
    if not build.get('passed'):
        raise RuntimeError('The selected build has not passed its acceptance gates')
    artifact = build['artifacts']['lumen']
    binary = (root / artifact['path']).resolve()
    compiled_manifest_path = args.build_summary.resolve().parent / 'compiled-source-sha256.json'
    if sha(compiled_manifest_path) != build['sourceManifestSHA256']:
        raise RuntimeError('Frozen compiled source manifest changed')
    compiled_sources = read(compiled_manifest_path)

    def verify_compiled_sources():
        if (sha(binary) != artifact['sha256']
                or sha(compiled_manifest_path) != build['sourceManifestSHA256']):
            raise RuntimeError('Frozen native artifact or compiled source manifest changed')
        for name, digest in compiled_sources.items():
            if sha(root / name) != digest:
                raise RuntimeError('Compiled production source differs: ' + name)

    verify_compiled_sources()
    selection = read(evidence / 'frozen-selection.json')
    schedule = selection['schedule']
    digest = hashlib.sha256(json.dumps(schedule, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    if digest != SCHEDULE or selection['wptRevision'] != PIN or len(schedule) != 53 or len(selection['sources']) != 27:
        raise RuntimeError('Frozen supplemental selection identity changed')
    closure_path = evidence / 'frozen-source-closure.json'
    if sha(closure_path) != CLOSURE:
        raise RuntimeError('Pinned source closure manifest changed')
    closure = read(closure_path)
    if closure['wptRevision'] != PIN or len(closure['sourceFiles']) != 4063:
        raise RuntimeError('Pinned source closure is incomplete')

    def verify_closure():
        for entry in closure['sourceFiles']:
            path = (upstream / entry['path']).resolve()
            path.relative_to(upstream.resolve())
            if path.stat().st_size != entry['bytes'] or sha(path) != entry['sha256']:
                raise RuntimeError('Pinned upstream source differs: ' + entry['path'])

    previous = read(root / 'agent/evidence/10.292019r-mac004-final-addendum/xhr53-receipt.json')
    config_path, environment_path = fixture / 'server.config.json', fixture / 'environment.json'
    for path in (config_path, environment_path, fixture / 'receipt.json'):
        if sha(path) != previous['protocolInputs'][str(path.relative_to(root))]:
            raise RuntimeError('Prior qualified fixture input changed: ' + str(path))
    config, environment, prior_fixture = read(config_path), read(environment_path), read(fixture / 'receipt.json')
    if not prior_fixture['accepted'] or environment['sourceRevision'] != PIN:
        raise RuntimeError('The supplemental fixture was not previously qualified')
    if hashlib.sha256(json.dumps(read(fixture / 'selection.json')['schedule'], sort_keys=True, separators=(',', ':')).encode()).hexdigest() != SCHEDULE:
        raise RuntimeError('The live fixture selection differs from the frozen53')
    for name, digest in selection['sources'].items():
        if sha(upstream / name) != digest:
            raise RuntimeError('Selected upstream test changed: ' + name)
    verify_closure()
    common = load('xhr53_unchanged_collector', root / 'tests/wpt/full_runner.py')
    audit = load('xhr53_independent_comparison', root / 'tools/apple-session-gpt6as/audit_wpt_candidate.py')
    namespace_error = common.monitor_module().pid_namespace_error()
    if namespace_error:
        raise RuntimeError(namespace_error)
    protocol = {}
    for name in ('tests/wpt/full_runner.py', 'tools/monitor_process.py', 'tests/wpt/testharnessreport.js'):
        if sha(root / name) != previous['protocolInputs'][name]:
            raise RuntimeError('Qualified execution protocol changed: ' + name)
        protocol[name] = sha(root / name)
    for path in (config_path, environment_path, fixture / 'receipt.json', closure_path,
                 evidence / 'frozen-selection.json', compiled_manifest_path,
                 args.build_summary.resolve(), Path(__file__).resolve()):
        protocol[str(path.relative_to(root))] = sha(path)
    baseline_path = root / 'out/10292019r-mac/xhr-native004-001/finalized-results.json'
    if sha(baseline_path) != previous['completeAggregateSHA256']:
        raise RuntimeError('Native baseline raw results differ from the retained receipt')
    baseline = read(baseline_path)['rows']
    reference_path = evidence / 'official-chrome-reference-results.jsonl'
    reference_receipt = read(evidence / 'official-chrome-reference-receipt.json')
    reference_audit = read(evidence / 'reference-audit.json')
    if (sha(reference_path) != reference_audit['files'][reference_path.name]
            or reference_receipt['binarySHA256'] != CHROME
            or reference_receipt['scheduleSHA256'] != SCHEDULE or reference_receipt['wptRevision'] != PIN):
        raise RuntimeError('Qualified Chromium reference identity changed')
    reference = [json.loads(line) for line in reference_path.read_text().splitlines()]
    if [row['test'] for row in reference] != [row['test'] for row in schedule]:
        raise RuntimeError('Chromium reference denominator differs')
    receipt = {'schema': 1, 'startedUTC': utc(), 'scope': selection['scope'], 'officialWptFullRate': None,
        'binary': artifact, 'buildSummarySHA256': sha(args.build_summary),
        'reviewedProductSource': build['reviewedProductSource'], 'nativeGui': True,
        'compiledSourceManifestSHA256': build['sourceManifestSHA256'], 'compiledSourceVerifiedBefore': True,
        'wptRevision': PIN, 'scheduleSHA256': SCHEDULE, 'scheduled': 53,
        'sourceClosureSHA256': CLOSURE, 'verifiedSourceFiles': 4063, 'protocolInputs': protocol,
        'fixtureOrigins': environment['origins'], 'sourceClosureVerifiedBefore': True,
        'sourceFixtureMutated': False, 'recorded': 0, 'unrun': 53, 'accepted': False}
    save(out / 'receipt.json', receipt)
    save(out / 'selection.json', selection)
    start = time.monotonic()
    server = sampler = log = None
    rows = []
    try:
        ports = [port for values in config['ports'].values() for port in values if port is not None]
        for port in ports:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', port))
        venv = root / 'output/wpt-10290244/upstream/_venv3'
        if not (venv / 'bin/python').is_file():
            raise RuntimeError('Existing qualified WPT Python environment is unavailable')
        command = [str(venv / 'bin/python'), str(upstream / 'wpt'), '--venv', str(venv),
                   '--skip-venv-setup', 'serve', '--config', str(config_path)]
        log = (out / 'fixture.log').open('wb')
        server = subprocess.Popen(command, cwd=upstream, stdout=log, stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL, start_new_session=True)
        sampler = common.Sampler(server.pid, out / 'fixture.metrics.jsonl')
        receipt['fixtureCommand'] = command
        receipt['fixturePID'] = server.pid

        def get(port, resource, secure=False):
            host = 'web-platform.test:' + str(port)
            if secure:
                context = ssl.create_default_context(cafile=environment['nativeEnvironment']['SSL_CERT_FILE'])
                with socket.create_connection(('127.0.0.1', port), timeout=3) as raw:
                    with context.wrap_socket(raw, server_hostname='web-platform.test') as channel:
                        channel.sendall(('GET ' + resource + ' HTTP/1.1\r\nHost: ' + host + '\r\nConnection: close\r\n\r\n').encode())
                        response = http.client.HTTPResponse(channel)
                        response.begin()
                        return response.status, response.read(1024 * 1024)
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
            try:
                connection.request('GET', resource, headers={'Host': host})
                response = connection.getresponse()
                return response.status, response.read(1024 * 1024)
            finally:
                connection.close()

        deadline, last_error = time.monotonic() + 40, None
        while time.monotonic() < deadline:
            if server.poll() is not None:
                raise RuntimeError('Fresh official fixture exited; inspect fixture.log')
            try:
                probes = {}
                for port in (28800, 28801, 28802, 28443, 28444, 28445):
                    secure = port < 28800
                    status, body = get(port, '/resources/testharnessreport.js', secure)
                    digest = hashlib.sha256(body).hexdigest()
                    if status != 200 or digest != protocol['tests/wpt/testharnessreport.js']:
                        raise RuntimeError('Official reporter route differs on port ' + str(port))
                    probes[str(port)] = {'status': status, 'reporterSHA256': digest, 'certificateVerified': secure}
                for resource, expected in [('/common/get-host-info.sub.js', prior_fixture['hostInfoGeneratedSHA256']),
                    ('/xhr/abort-after-send.any.worker.html', prior_fixture['workerWrapperSHA256']),
                    ('/resources/WebIDLParser.js', prior_fixture['helperSHA256']['resources/webidl2/lib/webidl2.js'])]:
                    status, body = get(28800, resource)
                    if status != 200 or hashlib.sha256(body).hexdigest() != expected:
                        raise RuntimeError('Pinned official helper route differs: ' + resource)
                receipt['freshReadiness'] = probes
                break
            except Exception as failure:
                last_error = str(failure)
                time.sleep(0.2)
        if 'freshReadiness' not in receipt:
            raise RuntimeError('Fresh fixture readiness failed: ' + str(last_error))
        for key in tuple(os.environ):
            if key.startswith(('LUMEN_', 'JSRUN_')):
                del os.environ[key]
        os.environ.update(environment['nativeEnvironment'])
        options = argparse.Namespace(origin=environment['origins']['http'],
            secure_origin=environment['origins']['https'], h2_origin=environment['origins']['h2'])
        with (out / 'results.jsonl').open('x') as stream:
            for index, test in enumerate(schedule):
                if time.monotonic() - start >= args.max_seconds:
                    raise TimeoutError('Bounded XHR replay budget reached; remaining variants stay unrun')
                if sha(binary) != artifact['sha256'] or any(sha(root / name) != digest for name, digest in protocol.items()):
                    raise RuntimeError('Execution identity changed before case')
                url = common.test_url(test, options)
                prefix = out / ('case-' + format(index, '03d'))
                limit = 65 if test['timeout'] == 'long' else 15
                result = common.run_lumen(binary, url, limit, prefix, root / 'tests/wpt/testharnessreport.js')
                row = {**test, 'sequence': index, 'url': url, **result}
                save(prefix.with_suffix('.result.json'), row)
                rows.append(row)
                stream.write(json.dumps(row) + '\n')
                stream.flush()
                os.fsync(stream.fileno())
                receipt.update(recorded=len(rows), unrun=53-len(rows))
                save(out / 'receipt.json', receipt)
                print(json.dumps({'sequence': index, 'test': test['test'], 'status': result['status'],
                    'pass': sum(check['status'] == 'PASS' for check in result.get('subtests', [])),
                    'total': len(result.get('subtests', [])), 'durationMs': result['duration_ms']}), flush=True)
                if common.measurement_failure(result) or audit.evidence_error(row):
                    raise RuntimeError('Measurement or cleanup failed; preserve all results')
                if sha(binary) != artifact['sha256'] or any(sha(root / name) != digest for name, digest in protocol.items()):
                    raise RuntimeError('Execution identity changed during case')
        if [json.loads(line) for line in (out / 'results.jsonl').read_text().splitlines()] != rows:
            raise RuntimeError('Persisted raw ledger differs from collected records')
        verify_closure()
        receipt['sourceClosureVerifiedAfter'] = True
        verify_compiled_sources()
        receipt['compiledSourceVerifiedAfter'] = True
        receipt['summary'] = audit.summarize_rows(rows, 53)
        native_comparison = audit.audit_rows(baseline, rows)
        chrome_comparison = audit.audit_rows(reference, rows)
        save(out / 'baseline-comparison.json', native_comparison)
        save(out / 'chromium-comparison.json', chrome_comparison)
        receipt['comparisons'] = {'nativeBaseline': {'noRegression': native_comparison['noRegression'],
            'lostPassingAssertions': len(native_comparison['lostPassingAssertions']),
            'gainedPassingAssertions': len(native_comparison['gainedPassingAssertions'])},
            'qualifiedChromiumReference': {'binarySHA256': CHROME,
                'referencePASS': reference_receipt['summary']['assertions']['PASS'],
                'missingReferencePASS': len(chrome_comparison['lostPassingAssertions']),
                'matchedReferencePASS': reference_receipt['summary']['assertions']['PASS'] - len(chrome_comparison['lostPassingAssertions'])}}
        receipt['accepted'] = len(rows) == 53 and native_comparison['noRegression']
    except Exception as failure:
        receipt['error'] = type(failure).__name__ + ': ' + str(failure)
    finally:
        if sampler:
            receipt['fixtureMetrics'] = sampler.stop()
        if server:
            receipt['fixtureCleanup'] = common.terminate_owned(server, sampler, verification_wait_s=5)
            if receipt['fixtureCleanup'].get('error') or receipt['fixtureCleanup'].get('process_group_gone') is not True:
                receipt['accepted'] = False
                receipt['cleanupError'] = 'Owned supplemental fixture did not cleanly disappear'
        if log:
            log.close()
        if receipt.get('fixtureMetrics', {}).get('errors'):
            receipt['accepted'] = False
        receipt.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - start, 6),
            recorded=len(rows), unrun=53-len(rows), binarySHA256After=sha(binary))
        receipt['summary'] = audit.summarize_rows(rows, 53)
        if receipt['binarySHA256After'] != artifact['sha256']:
            receipt['accepted'] = False
        receipt['rawFiles'] = [{'path': str(path.relative_to(out)), 'bytes': path.stat().st_size,
            'sha256': sha(path)} for path in sorted(out.iterdir()) if path.is_file() and path.name != 'receipt.json']
        save(out / 'receipt.json', receipt)
    print(json.dumps({key: receipt.get(key) for key in ('accepted', 'summary', 'comparisons',
        'durationSeconds', 'error', 'cleanupError')}), flush=True)
    return 0 if receipt['accepted'] and not receipt.get('error') else 1


if __name__ == '__main__':
    raise SystemExit(main())

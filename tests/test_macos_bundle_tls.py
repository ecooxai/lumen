#!/usr/bin/env python3
"""Actual native macOS GUI checks for bundled CA trust and explicit override.

Uses a temporary loopback CA/server and a separately signed test-only app copy.
The development delivery app, global trust stores, TLS verification and system
settings remain unchanged. Private keys stay in the ignored owned output tree.
"""
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import ssl
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, urlsplit


def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-summary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if sys.platform != 'darwin' or str(root) != '/Users/devin/repos/lumen':
        parser.error('Run on the actual canonical Mac workspace')
    sys.path.insert(0, str(root / 'tools'))
    from macos_bundle_dependencies import verify_recorded_bundle
    from monitor_macos_host_10300110 import HostSampler
    package_path = args.package_summary.resolve()
    package_path.relative_to(root / 'out')
    package = json.loads(package_path.read_text())
    if not package.get('passed'):
        parser.error('The relocated app dependency/runtime acceptance must pass first')
    app = (root / package['relocation']['app']).resolve()
    app.relative_to(root / 'out')
    executable = app / 'Contents/MacOS/lumen'
    ca_resource = app / 'Contents/Resources/cacert.pem'
    expected_binary = package['package']['binarySha256']
    if sha(executable) != expected_binary:
        parser.error('The accepted package executable differs')
    bundle_verified = verify_recorded_bundle(package['relocation']['deploymentClosure'], app,
        package['package']['runtimeResources'])
    original_ca = sha(ca_resource)
    out = args.output.resolve(); out.relative_to(root / 'out')
    out.mkdir(parents=True, exist_ok=False); out.chmod(0o700)
    private = out / 'private-fixture'; private.mkdir(mode=0o700)
    os.umask(0o077)
    started = time.monotonic()
    report = {'startedUTC': datetime.now(timezone.utc).isoformat(), 'passed': False,
        'scope': 'Four real native GUI loopback TLS trust controls; not WPT or a global trust-store change',
        'packageSummarySHA256': sha(package_path), 'packageBinarySHA256': expected_binary,
        'originalCAResourceSHA256': original_ca, 'testFixtureSHA256': sha(Path(__file__)),
        'sourceAppModified': False, 'systemTrustModified': False, 'cases': []}
    report['recordedRuntimeClosureVerifiedBefore'] = bundle_verified
    servers, threads = [], []

    def save():
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')

    def command(argv, limit=20):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=limit)
        if result.returncode:
            raise RuntimeError('Fixture/signing command failed: ' + argv[0] + ': ' + result.stderr[-400:])
        return result

    host_sampler = None
    try:
        host_sampler = HostSampler(out / 'host-telemetry', max_seconds=150)
        host_sampler.start()
        openssl = Path('/opt/homebrew/opt/openssl@3/bin/openssl')
        if not openssl.is_file():
            raise RuntimeError('Observed compatible OpenSSL fixture tool is unavailable')
        ca_key, ca = private / 'ca.key', private / 'ca.pem'
        server_key, csr, certificate = private / 'server.key', private / 'server.csr', private / 'server.pem'
        ca_config = private / 'ca.cnf'
        ca_config.write_text('[req]\nprompt=no\ndistinguished_name=dn\nx509_extensions=ca\n'
            '[dn]\nCN=Lumen task10300110 temporary loopback CA\n[ca]\n'
            'basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n')
        server_config = private / 'server.cnf'
        server_config.write_text('[req]\nprompt=no\ndistinguished_name=dn\nreq_extensions=server\n'
            '[dn]\nCN=localhost\n[server]\nbasicConstraints=critical,CA:FALSE\n'
            'keyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\n')
        for key in (ca_key, server_key):
            command([str(openssl), 'ecparam', '-name', 'prime256v1', '-genkey', '-noout', '-out', str(key)])
        command([str(openssl), 'req', '-new', '-x509', '-key', str(ca_key), '-out', str(ca),
            '-days', '2', '-config', str(ca_config)])
        command([str(openssl), 'req', '-new', '-key', str(server_key), '-out', str(csr), '-config', str(server_config)])
        command([str(openssl), 'x509', '-req', '-in', str(csr), '-CA', str(ca), '-CAkey', str(ca_key),
            '-CAcreateserial', '-out', str(certificate), '-days', '2', '-extfile', str(server_config), '-extensions', 'server'])
        command([str(openssl), 'verify', '-CAfile', str(ca), str(certificate)])
        report['temporaryPublicCA_SHA256'] = sha(ca)
        report['temporaryServerCertificateSHA256'] = sha(certificate)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, server_key)
        results, accepted, rejected = {}, [], []

        class TLSHandler(BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path != '/probe':
                    self.send_error(404); return
                accepted.append(time.monotonic())
                body = b'LUMEN_LOOPBACK_CERTIFICATE_VERIFIED'
                self.send_response(200)
                self.send_header('Access-Control-Allow-Origin', '*')
                self.send_header('Content-Type', 'text/plain')
                self.send_header('Content-Length', str(len(body)))
                self.send_header('Connection', 'close')
                self.end_headers(); self.wfile.write(body)
            def log_message(self, *_):
                pass

        class TLSServer(ThreadingHTTPServer):
            daemon_threads = True
            def get_request(self):
                raw, address = super().get_request(); raw.settimeout(4)
                try:
                    return context.wrap_socket(raw, server_side=True), address
                except ssl.SSLError as error:
                    rejected.append(error.reason); raw.close(); raise
                except Exception:
                    raw.close(); raise

        tls = TLSServer(('127.0.0.1', 0), TLSHandler)
        tls_url = 'https://localhost:' + str(tls.server_port) + '/probe'

        class PageHandler(BaseHTTPRequestHandler):
            def do_GET(self):
                parsed = urlsplit(self.path)
                case = parse_qs(parsed.query).get('case', [''])[0]
                if parsed.path != '/page' or case not in names:
                    self.send_error(404); return
                script = ('fetch(' + json.dumps(tls_url) + ').then(r=>r.text()).then(text=>({ok:true,text}),'
                    'e=>({ok:false,error:String(e)})).then(value=>fetch("/result?case="+' + json.dumps(case)
                    + ',{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(value)}))')
                body = ('<!doctype html><meta charset="utf-8"><title>Lumen TLS control</title>'
                    '<h1>Lumen bundled trust control</h1><p>Owned loopback endpoint</p><script>' + script + '</script>').encode()
                self.send_response(200); self.send_header('Content-Type', 'text/html')
                self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
            def do_POST(self):
                parsed = urlsplit(self.path)
                case = parse_qs(parsed.query).get('case', [''])[0]
                length = int(self.headers.get('Content-Length', '0'))
                if parsed.path != '/result' or case not in names or not 0 < length <= 2048:
                    self.send_error(400); return
                if case in results:
                    self.send_error(409); return
                results[case] = json.loads(self.rfile.read(length))
                self.send_response(204); self.end_headers()
            def log_message(self, *_):
                pass

        test_app = out / 'test-only-local-ca.app'
        shutil.copytree(app, test_app)
        shutil.copyfile(ca, test_app / 'Contents/Resources/cacert.pem')
        command(['/usr/bin/codesign', '--force', '--timestamp=none', '--sign', '-', str(test_app)], 30)
        command(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(test_app)], 20)
        report['testOnlyTrustVariant'] = {'path': str(test_app.relative_to(root)),
            'binarySHA256': sha(test_app / 'Contents/MacOS/lumen'),
            'changedResource': 'Contents/Resources/cacert.pem',
            'scope': 'Signed test-only copy with temporary loopback CA; never the distributed app'}
        cases = [('release-rejects-untrusted', app, None, False),
                 ('explicit-trust-override', app, ca, True),
                 ('bundled-local-ca', test_app, None, True),
                 ('explicit-override-wins', test_app, ca_resource, False)]
        names = {case[0] for case in cases}
        page = ThreadingHTTPServer(('127.0.0.1', 0), PageHandler)
        for server in (tls, page):
            servers.append(server)
            thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .05}, daemon=True)
            threads.append(thread); thread.start()
        base_env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
        base_env['DEVELOPER_DIR'] = '/Library/Developer/CommandLineTools'
        for name, case_app, override, expect_accept in cases:
            profile = out / (name + '-profile'); profile.mkdir()
            env = dict(base_env, LUMEN_PROFILE_DIR=str(profile), LUMEN_CONSOLE='1', LUMEN_NO_SIDEBAR='1',
                LUMEN_WINDOW='800x600', LUMEN_SHOT_MS='5000', LUMEN_SHOT=str(out / (name + '.png')))
            env.pop('SSL_CERT_FILE', None)
            if override is not None:
                env['SSL_CERT_FILE'] = str(override)
            before_accept, before_reject = len(accepted), len(rejected)
            prefix = out / name
            completed = subprocess.run([sys.executable, 'tools/monitor_process.py', '--duration', '20', '--settle', '0',
                '--output', str(prefix.relative_to(root)), '--', str(case_app / 'Contents/MacOS/lumen'),
                'http://localhost:' + str(page.server_port) + '/page?case=' + name],
                env=env, capture_output=True, text=True, timeout=55)
            (out / (name + '-collector.log')).write_text(completed.stdout + completed.stderr)
            monitor = json.loads(prefix.with_suffix('.json').read_text())
            value = results.get(name)
            alerts = rejected[before_reject:]
            certificate_alert = any(any(token in str(reason) for token in
                ('UNKNOWN_CA', 'CERTIFICATE_UNKNOWN', 'BAD_CERTIFICATE')) for reason in alerts)
            outcome = (monitor.get('natural_exit_code') == 0 and not monitor.get('ended_by_monitor')
                and not monitor.get('collector_error') and isinstance(value, dict)
                and not monitor.get('owned_group_cleanup', {}).get('error')
                and value.get('ok') == expect_accept
                and (value.get('text') == 'LUMEN_LOOPBACK_CERTIFICATE_VERIFIED'
                    and len(accepted) == before_accept + 1 if expect_accept
                    else len(accepted) == before_accept and certificate_alert))
            report['cases'].append({'name': name, 'expectedCertificateAccepted': expect_accept,
                'passed': outcome, 'browserResult': value, 'serverAcceptedRequests': len(accepted)-before_accept,
                'serverTLSAlerts': alerts, 'certificateSpecificRejection': certificate_alert,
                'explicitCAOverride': override is not None, 'durationSeconds': monitor['duration_s'],
                'monitorSHA256': sha(prefix.with_suffix('.json')), 'logSHA256': sha(prefix.with_suffix('.log')),
                'intervalCPU_P95Percent': monitor.get('steady_interval_cpu_p95_percent'),
                'rssPeakMiB': monitor.get('rss_peak_mb'), 'maxSamplingGapSeconds': monitor.get('max_sampling_gap_s'),
                'cpuUnit': monitor.get('cpu_unit')})
            save()
            if not outcome:
                raise RuntimeError(name + ' did not prove its declared native certificate behavior')
        if sha(executable) != expected_binary or sha(ca_resource) != original_ca:
            raise RuntimeError('The delivery app changed during trust tests')
        report['recordedRuntimeClosureVerifiedAfter'] = verify_recorded_bundle(package['relocation']['deploymentClosure'], app,
            package['package']['runtimeResources'])
        if sha(Path(__file__)) != report['testFixtureSHA256']:
            raise RuntimeError('The TLS fixture changed during execution')
        report['passed'] = True
    except Exception as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        for server in servers:
            server.shutdown(); server.server_close()
        for thread in threads:
            thread.join(timeout=5)
        if host_sampler is not None:
            report['hostTelemetry'] = host_sampler.finish()
            if report['passed'] and not report['hostTelemetry']['validOneSecondCoverage']:
                report.update(passed=False, error='Required one-second whole-host telemetry was incomplete')
        report['finishedUTC'] = datetime.now(timezone.utc).isoformat()
        report['durationSeconds'] = round(time.monotonic() - started, 3)
        save()
        print(json.dumps({'passed': report['passed'], 'casesPassed': sum(row['passed'] for row in report['cases']),
            'casesRequired': 4, 'summary': str((out / 'summary.json').relative_to(root)),
            'error': report.get('error'), 'durationSeconds': report['durationSeconds']}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

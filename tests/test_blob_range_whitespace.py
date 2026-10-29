#!/usr/bin/env python3
"""Public XHR Blob Range regression in real Window and DedicatedWorker contexts.

These sixteen custom checks are separate from upstream WPT. Neither the browser
APIs nor the transport are mocked. Browser profile and process ownership remain
isolated; every run retains its actual binary, fixture and raw-output identity.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import http.server
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CASES = [
    {'range': 'bytes \t =\t 1 - 2', 'status': 206, 'text': '12'},
    {'range': 'b ytes=0-1', 'status': 0, 'text': ''},
    {'range': 'bytes=1 0-11', 'status': 0, 'text': ''},
    {'range': 'bytes=0-1 2', 'status': 0, 'text': ''},
    {'range': 'bytes=\t0-\t1', 'status': 206, 'text': '01'},
    {'range': 'bytes=-0', 'status': 0, 'text': ''},
    {'range': 'bytes=-3', 'status': 206, 'text': 'def'},
    {'range': 'bytes=99999-', 'status': 0, 'text': ''},
]
MARKER = 'BLOB_RANGE_RESULTS '
CASE_MARKER = 'BLOB_RANGE_CASE '
RUN_CASES = r'''async function runCases(context) {
  const output = [];
  for (const [index, expected] of CASES.entries()) {
    const url = URL.createObjectURL(new Blob(['0123456789abcdef'], {type: 'text/plain'}));
    const xhr = new XMLHttpRequest();
    let actual;
    try {
      actual = await new Promise((resolve, reject) => {
        const timer = setTimeout(() => { xhr.abort(); reject(new Error('XHR case timeout')); }, 3000);
        xhr.open('GET', url);
        xhr.setRequestHeader('Range', expected.range);
        xhr.onloadend = () => {
          clearTimeout(timer);
          try { resolve({status: xhr.status, text: xhr.responseText}); }
          catch (error) { reject(error); }
        };
        xhr.send();
      });
    } catch (error) { actual = {error: error.name + ': ' + error.message}; }
    finally { URL.revokeObjectURL(url); }
    output.push({context, index, range: expected.range, expected, actual,
      ok: actual.status === expected.status && actual.text === expected.text});
  }
  return output;
}
'''.replace('CASES', json.dumps(CASES))
WORKER = RUN_CASES + '\nonmessage = async () => postMessage(await runCases("worker"));\n'
PAGE = ('<!doctype html><meta charset="utf-8"><title>XHR Blob Range regression</title><script>' + RUN_CASES + r'''
(async () => {
  try {
    const rows = await runCases('window');
    const worker = new Worker('/worker.js');
    const workerRows = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Worker fixture timeout')), 10000);
      worker.onmessage = event => { clearTimeout(timer); resolve(event.data); };
      worker.onerror = event => { clearTimeout(timer); reject(new Error(event.message)); };
      worker.postMessage(null);
    });
    worker.terminate();
    rows.push(...workerRows);
    for (const row of rows) console.log('BLOB_RANGE_CASE ' + JSON.stringify(row));
    console.log('BLOB_RANGE_RESULTS ' + JSON.stringify({count: rows.length}));
  } catch (error) { console.log('BLOB_RANGE_RESULTS ' + JSON.stringify({error: error.name + ': ' + error.message})); }
})();
</script>''').encode()


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def common_module():
    spec = importlib.util.spec_from_file_location('blob_range_test_common', ROOT / 'tests/wpt/full_runner.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/worker.js':
            body, kind = WORKER.encode(), 'application/javascript'
        elif self.path == '/':
            body, kind = PAGE, 'text/html; charset=utf-8'
        else:
            self.send_error(404); return
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)

    def log_message(self, *_):
        pass


def run_native(common, binary, url, prefix, runtime):
    environment = {key: value for key, value in os.environ.items() if not key.startswith(('JSRUN_', 'LUMEN_'))}
    if set(runtime) - {'LUMEN_ICU_DATA', 'LD_LIBRARY_PATH'}:
        raise ValueError('Unexpected runtime environment key')
    environment.update(runtime)
    profile = Path(tempfile.mkdtemp(prefix='blob-range-native-', dir=prefix.parent))
    environment.update(LUMEN_CONSOLE='1', LUMEN_PROFILE_DIR=str(profile), LUMEN_DOWNLOAD_DIR=str(profile / 'downloads'))
    (profile / 'downloads').mkdir()
    process, sampler, metrics, cleanup = None, None, None, None
    try:
        with prefix.with_suffix('.stdout.log').open('wb') as log:
            process = subprocess.Popen([str(binary), url, '25000'], cwd=ROOT, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            sampler = common.Sampler(process.pid, prefix.with_suffix('.metrics.jsonl'))
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                text = prefix.with_suffix('.stdout.log').read_text(errors='replace')
                if MARKER in text or process.poll() is not None:
                    break
                time.sleep(.025)
        returncode = process.poll()
    finally:
        if sampler: metrics = sampler.stop()
        if process: cleanup = common.terminate_owned(process, sampler)
        if cleanup and not cleanup.get('error'):
            import shutil
            shutil.rmtree(profile)
    return {'stdout': text, 'metrics': metrics, 'cleanup': cleanup, 'naturalExitCode': returncode}


def run_chromium(common, binary, url, prefix):
    browser = common.Chromium(binary, prefix.parent / (prefix.name + '-browser'), ['--no-sandbox', '--disable-gpu'], headless=True)
    page, context = None, None
    output = []
    try:
        context = browser.browser.call('Target.createBrowserContext')['browserContextId']
        target = browser.browser.call('Target.createTarget', {'url': 'about:blank', 'browserContextId': context})['targetId']
        with urllib.request.urlopen('http://127.0.0.1:' + str(browser.port) + '/json/list', timeout=5) as response:
            info = next(item for item in json.load(response) if item['id'] == target)
        page = common.CDP(info['webSocketDebuggerUrl'])
        page.call('Runtime.enable'); page.call('Page.enable')
        navigation = page.call('Page.navigate', {'url': url})
        if navigation.get('errorText'): raise RuntimeError(navigation['errorText'])
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try: event = page.event(.2)
            except socket.timeout: continue
            if event.get('method') == 'Runtime.consoleAPICalled':
                text = ' '.join(str(item.get('value', item.get('description', ''))) for item in event['params']['args'])
                output.append(text)
                if MARKER in text: break
    finally:
        if page: page.close()
        if context: browser.browser.call('Target.disposeBrowserContext', {'browserContextId': context})
        cleanup = browser.close()
    text = '\n'.join(output) + '\n'
    prefix.with_suffix('.stdout.log').write_text(text)
    return {'stdout': text, 'browserVersion': browser.version, 'cleanup': cleanup,
            'metrics': json.loads((browser.directory / 'chromium.metrics-summary.json').read_text())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--binary', type=Path); group.add_argument('--chromium', type=Path)
    parser.add_argument('--runtime-environment', type=Path)
    parser.add_argument('--source-manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists(): raise ValueError('Preserve previous receipt; choose a new output path')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    common = common_module(); error = common.monitor_module().pid_namespace_error()
    if error: raise RuntimeError(error)
    binary = (args.binary or args.chromium).resolve(strict=True)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    started, began = utc(), time.monotonic()
    record = {'kind': 'public XHR Blob Range Window and DedicatedWorker regression', 'officialWPT': False,
              'nativeLumenExecuted': bool(args.binary), 'chromiumControlExecuted': bool(args.chromium),
              'binary': str(binary), 'binarySHA256': sha(binary), 'fixtureSHA256': sha(__file__),
              'pageSHA256': hashlib.sha256(PAGE).hexdigest(), 'workerSHA256': hashlib.sha256(WORKER.encode()).hexdigest(),
              'cases': CASES, 'startedUTC': started, 'rows': [], 'infrastructureErrors': []}
    if args.source_manifest: record['sourceManifestSHA256'] = sha(args.source_manifest)
    try:
        url = 'http://127.0.0.1:' + str(server.server_port) + '/'
        runtime = json.loads(args.runtime_environment.read_text()) if args.runtime_environment else {}
        result = run_native(common, binary, url, args.output.with_suffix(''), runtime) if args.binary else run_chromium(common, binary, url, args.output.with_suffix(''))
        for name in ('metrics', 'cleanup', 'naturalExitCode', 'browserVersion'):
            if name in result: record[name] = result[name]
        messages = [json.loads(line.split(MARKER, 1)[1]) for line in result['stdout'].splitlines() if MARKER in line]
        if len(messages) != 1 or messages[0].get('error'): raise RuntimeError('Missing/failed single fixture completion: ' + str(messages)[:200])
        rows = [json.loads(line.split(CASE_MARKER, 1)[1]) for line in result['stdout'].splitlines() if CASE_MARKER in line]
        record['rows'] = rows
        if messages[0].get('count') != 16 or len(rows) != 16 or {(row['context'], row['index']) for row in rows} != {(context, index) for context in ('window', 'worker') for index in range(8)}:
            raise RuntimeError('Fixture case inventory differs')
        for row in rows:
            if row['expected'] != CASES[row['index']] or row['ok'] != (row['actual'].get('status') == row['expected']['status'] and row['actual'].get('text') == row['expected']['text']):
                raise RuntimeError('Case expectation or reported verdict differs')
        if record.get('metrics', {}).get('errors') or record.get('cleanup', {}).get('error') or record.get('cleanup', {}).get('errors'):
            raise RuntimeError('Measurement or owned cleanup failed')
        if sha(binary) != record['binarySHA256']:
            raise RuntimeError('Native/browser executable changed during the fixture')
    except Exception as error: record['infrastructureErrors'].append(type(error).__name__ + ': ' + str(error))
    finally: server.shutdown(); server.server_close(); thread.join(timeout=5)
    record.update(finishedUTC=utc(), durationSeconds=time.monotonic() - began, total=16,
                  passed=sum(row['ok'] for row in record['rows']))
    record['failed'] = sum(not row['ok'] for row in record['rows'])
    record['recorded'] = len(record['rows'])
    record['unrun'] = 16 - record['recorded']
    args.output.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({key: record[key] for key in ('kind', 'binarySHA256', 'total', 'recorded', 'passed', 'failed', 'unrun', 'infrastructureErrors', 'durationSeconds')}))
    raise SystemExit(0 if record['passed'] == 16 and not record['infrastructureErrors'] else 1)


if __name__ == '__main__': main()

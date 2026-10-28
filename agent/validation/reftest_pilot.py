#!/usr/bin/env python3
"""Bounded native WPT reftest pilot using upstream readiness and pixel decisions.

Each page gets a fresh profile. A readiness callback triggers the explicit native
SIGUSR1 viewport capture hook. No elapsed-time screenshot or cross-engine pixel
oracle is used. Unsupported reference graphs remain explicit results.
"""
import argparse, base64, collections, datetime, hashlib, json, os, pathlib
import resource, shutil, signal, subprocess, sys, tempfile, time, urllib.parse, uuid
from types import SimpleNamespace
from native_capture_guard import guarded_popen


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def manifest_entries(tree, parts=()):
    if isinstance(tree, dict):
        for key, value in tree.items():
            yield from manifest_entries(value, parts + (key,))
    elif isinstance(tree, list):
        for item in tree[1:]:
            if isinstance(item, list):
                yield item[0] or '/' + '/'.join(parts), {'source': '/'.join(parts), 'references': item[1], 'metadata': item[2]}


def absolute_url(url):
    if urllib.parse.urlsplit(url).scheme:
        return url
    scheme, port = ('https', 8443) if '.https.' in url.split('?')[0] else ('http', 8000)
    return '%s://web-platform.test:%d%s' % (scheme, port, url)


def image_details(path):
    from PIL import Image
    with Image.open(path) as image:
        rgb = image.convert('RGB')
        colors = rgb.getcolors(rgb.width * rgb.height) or []
        return {'width': rgb.width, 'height': rgb.height,
                'png_sha256': digest(path),
                'pixel_sha1': hashlib.sha1(rgb.tobytes()).hexdigest(),
                'unique_colors': len(colors),
                'dominant_colors': sorted(colors, reverse=True)[:6]}


def capture(args, url, directory, width=800, height=600, timeout=None):
    directory.mkdir(parents=True, exist_ok=False)
    full_url = absolute_url(url)
    result = {'url': url, 'requested_url': full_url, 'started_utc': utc(), 'status': 'ERROR'}
    if urllib.parse.urlsplit(full_url).scheme not in ('http', 'https'):
        return dict(result, status='UNSUPPORTED', reason='Non-HTTP reference scheme is outside this bounded pilot')
    token = uuid.uuid4().hex
    upstream = (args.wpt_root / 'tools/wptrunner/wptrunner/executors/test-wait.js').read_text()
    body = upstream % {'classname': 'reftest-wait'}
    marker = 'LUMEN_REF_READY_' + token + ' '
    pre = directory / 'readiness.js'
    prefix = "(function(){const emit=console.error.bind(console);function start(){try{(function(){\n"
    suffix = "\n}).call(window,location.href,function(value){emit(" + json.dumps(marker) + "+JSON.stringify({value:value,width:innerWidth,height:innerHeight,dpr:devicePixelRatio,readyState:document.readyState,fontStatus:document.fonts&&document.fonts.status}));});}catch(e){emit(" + json.dumps(marker) + "+JSON.stringify({error:String(e)}));}}if(document.readyState==='complete')start();else addEventListener('load',start,{once:true});})();\n"
    pre.write_text(prefix + body + suffix)
    result['readiness_script_sha256'] = digest(pre)
    profile = tempfile.mkdtemp(prefix='lrf-', dir=os.environ.get('TMPDIR'))
    png = directory / 'viewport.png'
    log_path = directory / 'browser.log'
    env = dict(os.environ, XDG_DATA_HOME=profile, LUMEN_NO_SESSION='1',
               LUMEN_HOST_MAP='web-platform.test=127.0.0.1,not-web-platform.test=127.0.0.1',
               SSL_CERT_FILE='/tmp/lumen-wpt/ca.pem', LUMEN_PRE_FILE=str(pre),
               LUMEN_WINDOW='%dx%d' % (width, height + 44), LUMEN_NO_SIDEBAR='1',
               LUMEN_CONSOLE='2', LUMEN_JS_TIMEOUT_MS='60000', LUMEN_TESTDRIVER='1',
               SDL_VIDEODRIVER='offscreen', LUMEN_NO_GPU='1', LUMEN_WPT_CAPTURE=str(png))
    start = time.monotonic()
    deadline = start + (timeout or args.timeout)
    proc = monitor = None
    ready = captured = captured_url = None
    sent = False
    failure = None
    try:
        with log_path.open('w') as log:
            proc = guarded_popen([str(args.binary), full_url], env=env, stdout=log,
                                    stderr=subprocess.STDOUT, start_new_session=True)
            result['pid'] = proc.pid
            monitor = subprocess.Popen([sys.executable, 'agent/progress/monitor.py',
                '--root-pid', str(proc.pid), '--output', str(directory / 'resources.csv'),
                '--label', 'native-reftest-viewport'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            with log_path.open() as reader:
                pending = ''
                while True:
                    chunk = reader.read(65536)
                    pending += chunk
                    lines = pending.split('\n')
                    pending = lines.pop()
                    for line in lines:
                        if marker in line:
                            payload = json.loads(line.split(marker, 1)[1])
                            if payload.get('error'):
                                failure = ('ERROR', 'Readiness adapter exception: ' + payload['error'])
                            elif isinstance(payload.get('value'), list) and len(payload['value']) > 1 and payload['value'][1] == 'complete':
                                ready = payload
                                if (payload.get('width'), payload.get('height'), payload.get('dpr')) != (width, height, 1):
                                    failure = ('UNSUPPORTED', 'Actual CSS viewport or DPR differs from requested viewport')
                                elif payload.get('fontStatus') != 'loaded':
                                    failure = ('UNSUPPORTED', 'Font readiness could not be verified')
                                elif payload['value'][0] != full_url:
                                    failure = ('ERROR', 'Readiness URL differs from requested URL')
                                elif not sent and proc.poll() is None:
                                    os.kill(proc.pid, signal.SIGUSR1)
                                    sent = True
                            else:
                                failure = ('UNSUPPORTED', 'Upstream readiness requested an unsupported automation action')
                        elif 'LUMEN_REF_CAPTURE {' in line:
                            captured = json.loads(line.split('LUMEN_REF_CAPTURE ', 1)[1])
                        elif 'LUMEN_REF_CAPTURE_URL ' in line:
                            captured_url = line.split('LUMEN_REF_CAPTURE_URL ', 1)[1]
                    if failure:
                        break
                    if proc.poll() is not None:
                        # Drain final complete log lines before interpreting exit.
                        if not chunk:
                            break
                    if time.monotonic() >= deadline:
                        failure = ('TIMEOUT', 'Native capture deadline after readiness' if sent else 'Upstream readiness did not complete before deadline')
                        break
                    if log_path.stat().st_size > 16 * 1024 * 1024:
                        failure = ('ERROR', 'Task-owned browser log exceeded bounded pilot limit')
                        break
                    time.sleep(.05)
        if failure:
            result.update(status=failure[0], reason=failure[1])
        elif proc.returncode != 0:
            result.update(status='CRASH' if proc.returncode is not None and proc.returncode < 0 else 'ERROR', reason='Browser exited before a valid capture')
        elif not ready or not captured or not captured.get('ok') or not png.exists():
            result.update(status='ERROR', reason='Missing ready/capture evidence or PNG')
        elif captured_url != full_url:
            result.update(status='ERROR', reason='Captured native page URL differs from requested URL')
        elif (captured.get('width'), captured.get('height'), captured.get('scale')) != (width, height, 1):
            result.update(status='UNSUPPORTED', reason='Native viewport dimensions or scale do not match')
        else:
            info = image_details(png)
            if (info['width'], info['height']) != (width, height):
                result.update(status='ERROR', reason='PNG dimensions differ from verified viewport')
            else:
                result.update(status='OK', image=info, png=str(png))
    except Exception as exc:
        result.update(status='ERROR', reason=type(exc).__name__ + ': ' + str(exc))
    finally:
        if proc and proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        if monitor and monitor.poll() is None:
            monitor.terminate()
            try:
                monitor.wait(timeout=5)
            except subprocess.TimeoutExpired:
                monitor.kill(); monitor.wait()
        shutil.rmtree(profile)
    result.update(finished_utc=utc(), elapsed_s=round(time.monotonic() - start, 4),
                  readiness=ready, capture=captured, captured_url=captured_url,
                  exit_code=proc.returncode if proc else None, log=str(log_path))
    (directory / 'capture.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


class EvidenceLogger:
    def __init__(self): self.messages = []
    def __getattr__(self, name):
        def log(message, *args, **kwargs): self.messages.append({'level': name, 'message': str(message)[:2000]})
        return log


def compare_images(implementation, first, second, urls, relation, fuzzy):
    implementation.message = []  # Same per-test state initialized by upstream run_test.
    encoded = [base64.b64encode(pathlib.Path(x['png']).read_bytes()).decode('ascii') for x in (first, second)]
    hashes = [[x['image']['pixel_sha1']] for x in (first, second)]
    passed, page = implementation.check_pass(hashes, [[encoded[0]], [encoded[1]]], urls, relation, fuzzy)
    maximum, pixels = implementation.get_differences(encoded, urls)
    return {'relation': relation, 'fuzzy': fuzzy, 'passed': passed, 'page_index': page,
            'max_channel_difference': maximum, 'different_pixels': pixels}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--urls-file', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--wpt-root', type=pathlib.Path, default='/mnt/p3/project/browser/wpt')
    parser.add_argument('--manifest', type=pathlib.Path, default='/mnt/p3/project/browser/wpt-data/MANIFEST.json')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--long-timeout', type=float, default=100)
    args = parser.parse_args()
    args.binary = args.binary.resolve(); args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    sys.path.insert(0, str(args.wpt_root / 'tools/wptrunner'))
    sys.path.insert(0, str(args.wpt_root / 'tools'))
    from wptrunner.executors.base import RefTestImplementation
    logger = EvidenceLogger()
    implementation = RefTestImplementation(SimpleNamespace(logger=logger, timeout_multiplier=1,
        subsuite='', screenshot_cache={}, reftest_screenshot='always'))
    manifest = dict(manifest_entries(json.loads(args.manifest.read_text())['items']['reftest']))
    urls = json.loads(args.urls_file.read_text())
    provenance = {'started_utc': utc(), 'binary': str(args.binary), 'binary_sha256': digest(args.binary),
        'manifest_sha256': digest(args.manifest), 'urls_sha256': digest(args.urls_file),
        'runner_sha256': digest(__file__), 'upstream_readiness_sha256': digest(args.wpt_root / 'tools/wptrunner/wptrunner/executors/test-wait.js'),
        'upstream_comparator_sha256': digest(args.wpt_root / 'tools/wptrunner/wptrunner/executors/base.py'),
        'selected': len(urls), 'viewport_css': [800,600], 'dpr': 1,
        'timeout_seconds': args.timeout, 'long_timeout_seconds': args.long_timeout,
        'oracle': 'Each engine test image versus its own reference; native uses pinned official WPT comparator.',
        'unsupported_scope': ['recursive reference graphs','non-HTTP reference URLs','testdriver actions during readiness','nondefault viewport/DPI']}
    (args.output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    results = []
    with (args.output / 'results.jsonl').open('w') as output:
        for index, url in enumerate(urls):
            row = {'url': url, 'started_utc': utc(), 'status': 'ERROR', 'comparisons': []}
            try:
                entry = manifest.get(url)
                if not entry:
                    row.update(status='UNSUPPORTED', reason='URL is not a pinned-manifest reftest')
                elif any(ref in manifest and manifest[ref]['references'] for ref, relation in entry['references']):
                    row.update(status='UNSUPPORTED', reason='Recursive reference graph is outside bounded pilot')
                elif entry['metadata'].get('viewport_size') or entry['metadata'].get('dpi'):
                    row.update(status='UNSUPPORTED', reason='Nondefault viewport or DPI metadata is outside bounded pilot')
                else:
                    timeout = args.long_timeout if entry['metadata'].get('timeout') == 'long' else args.timeout
                    row['metadata'] = entry['metadata']
                    test = capture(args, url, args.output / ('%03d' % index) / 'test', timeout=timeout)
                    row['test_capture'] = test
                    if test['status'] != 'OK':
                        row.update(status=test['status'], reason=test.get('reason'))
                    else:
                        outcomes = []
                        for ref_index, (reference, relation) in enumerate(entry['references']):
                            ref = capture(args, reference, args.output / ('%03d' % index) / ('ref-%d' % ref_index), timeout=timeout)
                            comparison = {'reference_url': reference, 'reference_capture': ref, 'relation': relation}
                            if ref['status'] != 'OK':
                                row.update(status=ref['status'], reason=ref.get('reason'));row['comparisons'].append(comparison);break
                            fuzzy = None
                            for key, value in entry['metadata'].get('fuzzy', []):
                                if key is None: fuzzy = value
                            for key, value in entry['metadata'].get('fuzzy', []):
                                if key == [url, reference, relation]: fuzzy = value
                            comparison.update(compare_images(implementation, test, ref, [url, reference], relation, fuzzy))
                            row['comparisons'].append(comparison);outcomes.append((relation, comparison['passed']))
                        else:
                            matches = [ok for relation, ok in outcomes if relation == '==']
                            mismatches = [ok for relation, ok in outcomes if relation == '!=']
                            if not outcomes:row.update(status='UNSUPPORTED',reason='No references')
                            else:row['status'] = 'PASS' if (not matches or any(matches)) and all(mismatches) else 'FAIL'
            except Exception as exc:
                row.update(status='ERROR', reason=type(exc).__name__ + ': ' + str(exc))
            row['finished_utc'] = utc();results.append(row)
            output.write(json.dumps(row) + '\n');output.flush()
            print(json.dumps({'url': url, 'status': row['status'], 'reason': row.get('reason')}), flush=True)
    summary = {'selected': len(urls), 'observed': len(results), 'statuses': dict(collections.Counter(x['status'] for x in results)),
               'finished_utc': utc(), 'note': 'Raw reftest outcomes; intentionally failing infrastructure controls are not relabeled PASS.'}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (args.output / 'comparator-log.json').write_text(json.dumps(logger.messages, indent=2) + '\n')
    print(json.dumps(summary), flush=True)
    return 0 if all(x['status'] == 'PASS' for x in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())

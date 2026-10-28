"""Measure owned native SDL scrolling or navigation/clone teardown, at 1 Hz.

The fixture uses the application's existing SDL wheel test hook, not OS input
injection. It never reads a user profile. Every verdict is bound to an immutable
binary and preserves the complete interval telemetry and native memory counters.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import signal
import statistics
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

ROOT = Path.cwd().resolve()
TELEMETRY = 'output/session-20261029/native-qa-kit-v1/source/scripts/qa-apple-gpt6/telemetry.py'
TELEMETRY_SHA = '70a9c367e6b7649e5469167c4431f7dc0cb4ad428a9d89ceb291e7834aaa4ec2'
MIB = 1048576
POLICY = {
    'sampling_seconds': 1, 'max_sampling_gap_seconds': 1.5,
    'initial_settle_seconds': 15, 'final_settle_seconds': 15,
    'final_idle_seconds_minimum': 45, 'idle_cpu_p95_ceiling_percent': 3,
    'scroll_cpu_p95_ceiling_percent': 10,
    'settled_rss_growth_ceiling_mib': 32, 'settled_rss_growth_ceiling_fraction': .20,
    'initial_rss_window': 'Last 5 samples of initial idle phase',
    'final_rss_window': 'Last 15 samples of final idle phase',
    'native_live_nodes_growth_allowance': 50, 'native_live_styles_growth_allowance': 50,
}


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def owned_path(value):
    path = (ROOT / value).resolve()
    path.relative_to(ROOT)
    return path


def atomic(path, data):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n')
    temporary.replace(path)


def stop(process):
    if process is None:
        return None
    forced = False
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            forced = True
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=3)
    return {'pid': process.pid, 'return_code': process.returncode, 'forced': forced}


def measured(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= 0


def phase_issues(summary, phase, duration, cpu_ceiling):
    group = summary.get('groups', {}).get('lumen', {}).get(phase, {})
    issues = []
    for key, minimum in [('observed_samples', max(10, duration)), ('duration_seconds', duration)]:
        if not measured(group.get(key)) or group[key] < minimum:
            issues.append(f'{phase}: incomplete {key}, expected at least {minimum}')
    if group.get('samples') != group.get('observed_samples'):
        issues.append(f'{phase}: missing process observations')
    cpu = group.get('cpu_p95_percent')
    if not measured(cpu):
        issues.append(f'{phase}: CPU p95 is unverified')
    elif cpu > cpu_ceiling:
        issues.append(f'{phase}: CPU p95 {cpu}% exceeds {cpu_ceiling}%')
    if group.get('cpu_status') not in ('within_threshold', 'measured_no_threshold'):
        issues.append(f'{phase}: telemetry did not produce a valid phase verdict')
    return issues


STYLE = '''<meta charset="utf-8"><style>
body{margin:0;background:#eaf4fc;color:#17364b;font:18px system-ui}
header{padding:28px;background:#dcebf6}h1{margin:0 0 10px;font-size:28px}
.row{height:32px;padding:0 28px;line-height:32px;border-bottom:1px solid #d1e0eb}
.row:nth-child(even){background:#f5f9fc}pre{padding:28px;white-space:pre-wrap}
</style>'''


def fixture(scenario, step, nonce):
    config = json.dumps({'scenario': scenario, 'step': step, 'nonce': nonce})
    if scenario == 'scroll':
        rows = ''.join(f'<div class="row">Native scroll verification · row {n}</div>' for n in range(1000))
        script = '''
let events=0,lastEventMs=0,maxY=0;
const scrolled=()=>{events++;lastEventMs=performance.now();maxY=Math.max(maxY,scrollY);};
window.addEventListener('scroll',scrolled);
document.addEventListener('scroll',scrolled,true);
send({kind:'ready',url:location.href});
setTimeout(()=>send({kind:'scroll-complete',events,lastEventMs,maxY,finalY:scrollY,url:location.href}),55000);
'''
        body = '<header><h1>Native SDL scroll measurement</h1>Existing application wheel hook · actual page scrolling</header>' + rows
    else:
        body = '<header><h1>Native navigation and document lifetime</h1>Identical workload on every page · real context teardown</header><pre id="result">Checking clone ownership…</pre>'
        script = '''
let checks=0;
const assert=(ok,message)=>{if(!ok)throw Error(message);checks++;};
try {
  for(let batch=0;batch<16;batch++){
    const d=document.implementation.createDocument('urn:lume:soak','p:root');
    for(let n=0;n<24;n++){
      const e=d.createElementNS('urn:lume:soak','p:item');
      const a=d.createAttributeNS('urn:lume:attribute','a:key');
      a.value='value-'+n;e.setAttributeNodeNS(a);e.removeAttributeNode(a);e.setAttributeNodeNS(a);
      d.documentElement.appendChild(e);
    }
    const copy=d.cloneNode(true),root=copy.documentElement,child=root.firstChild;
    assert(root.namespaceURI==='urn:lume:soak'&&root.prefix==='p','document clone qualified name');
    assert(child.ownerDocument===copy&&root.childNodes.length===24,'document clone ownership');
    assert(child.getAttributeNode('a:key').value==='value-0','cloned Attr value');
    const imported=document.importNode(root,true);
    assert(imported.ownerDocument===document&&imported.prefix==='p','import ownership and prefix');
    const attr=imported.firstChild.getAttributeNode('a:key');
    assert(attr.namespaceURI==='urn:lume:attribute'&&attr.ownerElement===imported.firstChild,'import Attr namespace and attachment');
  }
  document.getElementById('result').textContent=JSON.stringify({step:cfg.step,checks,documents:16},null,2);
  send({kind:'loaded',checks,documents:16,url:location.href});
  if(cfg.step<20)setTimeout(()=>{location.href='/page/'+(cfg.step+1)+'?nonce='+cfg.nonce;},cfg.step===0?30000:3000);
}catch(error){send({kind:'error',error:String(error),checks,url:location.href});}
'''
    report = '''const cfg=CONFIG;const send=value=>fetch('/report',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...value,nonce:cfg.nonce,step:cfg.step})}).catch(error=>console.error('fixture report failed',error));'''.replace('CONFIG', config)
    return ('<!doctype html><title>Lume native lifecycle verification</title>' + STYLE + body + '<script>' + report + script + '</script>').encode()


def run(args):
    binary, telemetry, output = map(owned_path, (args.binary, args.telemetry, args.output))
    if not binary.is_file() or sha(binary) != args.sha256:
        raise RuntimeError('Immutable native binary hash mismatch')
    if not telemetry.is_file() or sha(telemetry) != args.telemetry_sha256:
        raise RuntimeError('Pinned interval telemetry source hash mismatch')
    output.mkdir(parents=True, exist_ok=False)
    profile = output / 'profile'
    profile.mkdir()
    nonce = secrets.token_hex(16)
    pages = {step: fixture(args.scenario, step, nonce) for step in range(21 if args.scenario == 'navigation' else 1)}
    reports, counters, phases, issues = [], [], [], []
    lock = threading.Lock()
    app_started = None
    receipt = {'started_utc': utc(), 'scenario': args.scenario, 'binary': str(binary.relative_to(ROOT)),
               'binary_sha256': args.sha256, 'telemetry_sha256': args.telemetry_sha256,
               'profile': 'New isolated task-owned profile', 'policy': POLICY,
               'scope': 'Native Mac process tree; synthetic SDL wheel input or actual top-level navigation. No physical typing, IPA, simulator or full-browser benchmark claim.',
               'fixture_sha256_by_step': {str(step): hashlib.sha256(page).hexdigest() for step, page in pages.items()}}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            url = urlsplit(self.path)
            match = re.fullmatch(r'/page/(\d+)', url.path)
            step = int(match[1]) if match else -1
            if step not in pages or parse_qs(url.query).get('nonce') != [nonce]:
                self.send_error(404)
                return
            page = pages[step]
            self.send_response(200)
            self.send_header('Content-Type', 'text/html;charset=utf-8')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(page)))
            self.end_headers()
            self.wfile.write(page)

        def do_POST(self):
            if self.path != '/report':
                self.send_error(404)
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 8192:
                    raise ValueError('Invalid body size')
                value = json.loads(self.rfile.read(size))
                if value.pop('nonce', None) != nonce or value.get('step') not in pages:
                    raise ValueError('Wrong owned fixture nonce or page')
                with lock:
                    if len(reports) >= 64:
                        raise ValueError('Too many fixture reports')
                    reports.append({'utc': utc(), 'monotonic': time.monotonic(), 'value': value})
                self.send_response(200)
                self.send_header('Content-Length', '2')
                self.end_headers()
                self.wfile.write(b'{}')
            except (ValueError, TypeError):
                self.send_error(400)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    targets, samples = output / 'targets.json', output / 'process-samples.jsonl'
    app = sampler = reader = sampler_stream = None
    current_phase = None
    seconds = 115 if args.scenario == 'scroll' else 165

    def phase(name):
        nonlocal current_phase
        if name == current_phase:
            return
        current_phase = name
        phases.append({'phase': name, 'utc': utc(), 'elapsed_seconds': round(time.monotonic() - app_started, 3) if app_started else None})
        atomic(targets, {'phase': name, 'groups': [{'name': 'lumen', 'pids': [app.pid]}] if app else []})

    def log_reader(stream):
        total = 0
        with (output / 'lumen.log').open('xb') as log:
            for line in stream:
                total += len(line)
                if total > 2 * MIB:
                    issues.append('Owned application log exceeded 2 MiB')
                    break
                log.write(line)
                log.flush()
                text = line.decode('utf-8', 'replace')
                if 'fatal signal' in text or 'recovered from signal' in text:
                    issues.append('Native application reported a memory fault or crash recovery')
                if text.startswith('lumen-mem'):
                    values = {k: float(v) for k, v in re.findall(r'(\w+)=([\d.]+)', text)}
                    counters.append({'utc': utc(), 'elapsed_seconds': round(time.monotonic() - app_started, 3),
                                     'kind': text.split(':', 1)[0], 'values': values})

    try:
        phase('baseline')
        sampler_stream = (output / 'sampler.log').open('x')
        sampler = subprocess.Popen([sys.executable, str(telemetry), 'collect', '--targets', str(targets), '--output', str(samples), '--seconds', str(seconds + 30)],
                                   stdout=sampler_stream, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
        environment = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
        environment.update({'LUMEN_PROFILE_DIR': str(profile), 'LUMEN_CONSOLE': '1', 'LUMEN_NO_SIDEBAR': '1',
                            'LUMEN_WINDOW': '1000x700', 'LUMEN_MEM_STATS': '1', 'LUMEN_SHOT_MS': str(seconds * 1000),
                            'LUMEN_SHOT': str(output / f'gpt6-astra-pro_builtin-agents_devinmac3d_{args.scenario}.png')})
        if args.scenario == 'scroll':
            environment['LUMEN_AUTOSCROLL'] = '400,400,-0.08,15000,1875'
            receipt['input'] = {'source': 'Existing native SDL wheel hook', 'parameters': environment['LUMEN_AUTOSCROLL'],
                                'scheduled_event_count': 1875, 'scheduled_duration_seconds': 30}
        url = f'http://127.0.0.1:{server.server_port}/page/0?nonce={nonce}'
        app_started = time.monotonic()
        receipt['app_started_utc'] = utc()
        app = subprocess.Popen([str(binary), url], env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               stdin=subprocess.DEVNULL, start_new_session=True)
        receipt['app_pid'] = app.pid
        phase('startup')
        reader = threading.Thread(target=log_reader, args=(app.stdout,), daemon=True)
        reader.start()
        while app.poll() is None:
            now = time.monotonic()
            elapsed = now - app_started
            if issues:
                raise RuntimeError('Native log integrity check failed')
            if sampler.poll() is not None:
                raise RuntimeError('Sampler exited before the application')
            if elapsed > seconds + 20:
                raise RuntimeError('Application missed its natural screenshot/exit deadline')
            with lock:
                observed = list(reports)
            errors = [r for r in observed if r['value'].get('kind') == 'error']
            if errors:
                raise RuntimeError('Fixture failed: ' + errors[0]['value'].get('error', 'unknown'))
            if not observed and elapsed > 15:
                raise RuntimeError('Fixture did not report readiness within 15 seconds')
            if args.scenario == 'scroll':
                phase('scrolling' if 15 <= elapsed < 50 else 'settle:after' if 50 <= elapsed < 65 else 'idle:after' if elapsed >= 65 else 'startup')
            else:
                loaded = [r for r in observed if r['value'].get('kind') == 'loaded']
                if loaded:
                    latest = loaded[-1]
                    step = latest['value']['step']
                    age = now - latest['monotonic']
                    if step == 0:
                        phase('idle:before' if age >= 15 else 'startup')
                    elif step == 20:
                        phase('idle:after' if age >= 15 else 'settle:after')
                    else:
                        phase('navigation')
            time.sleep(.2)
        receipt['natural_app_return_code'] = app.returncode
        if app.returncode != 0:
            raise RuntimeError('Native application did not exit cleanly')
    except BaseException as error:
        issues.append(type(error).__name__ + ': ' + str(error))
    finally:
        phase('shutdown')
        receipt['app_termination'] = stop(app)
        receipt['sampler_termination'] = stop(sampler)
        server.shutdown()
        server.server_close()
        serving.join(timeout=2)
        if reader:
            reader.join(timeout=2)
            if reader.is_alive():
                issues.append('Application log reader did not finish')
        if sampler_stream:
            sampler_stream.close()
        receipt['finished_utc'] = utc()
        receipt['duration_seconds'] = round(time.monotonic() - app_started, 3) if app_started else None
        receipt['phase_transitions'] = phases
        receipt['reports'] = [{key: value for key, value in report.items() if key != 'monotonic'} for report in reports]
        receipt['native_memory_counters'] = counters
        receipt['binary_sha256_after'] = sha(binary)
        if receipt['binary_sha256_after'] != args.sha256:
            issues.append('Binary changed during observation')
        if (receipt.get('app_termination') or {}).get('forced') or (receipt.get('sampler_termination') or {}).get('forced'):
            issues.append('A task-owned process required forced termination')
        if (receipt.get('sampler_termination') or {}).get('return_code') != 0:
            issues.append('Telemetry collector did not exit cleanly')
        if not samples.is_file():
            issues.append('No raw telemetry exists')
        else:
            result = subprocess.run([sys.executable, str(telemetry), 'summarize', str(samples), '--output', str(output / 'process-summary.json')], capture_output=True, text=True, timeout=10)
            receipt['summary_exit_code'] = result.returncode
            if result.returncode:
                issues.append('Telemetry summary failed: ' + result.stderr[-1000:])
            else:
                summary = json.loads((output / 'process-summary.json').read_text())
                receipt['phase_metrics'] = summary.get('groups', {}).get('lumen', {})
                if summary.get('errors') or summary.get('sampling_gaps_over_1_5_seconds'):
                    issues.append('Telemetry contains errors or sampling gaps')
                issues.extend(phase_issues(summary, 'idle:after', 45, 3))
                if args.scenario == 'scroll':
                    issues.extend(phase_issues(summary, 'scrolling', 30, 10))
                else:
                    issues.extend(phase_issues(summary, 'idle:before', 10, 3))
                    records = [json.loads(line) for line in samples.read_text().splitlines()]
                    def rss_values(name):
                        return [r['groups']['lumen']['rss_bytes'] / MIB for r in records if r.get('type') == 'sample' and r.get('phase') == name and r.get('groups', {}).get('lumen', {}).get('observed')]
                    initial, final = rss_values('idle:before'), rss_values('idle:after')
                    if len(initial) < 5 or len(final) < 15:
                        issues.append('Insufficient settled RSS comparison samples')
                    else:
                        before, after = statistics.median(initial[-5:]), statistics.median(final[-15:])
                        growth = after - before
                        receipt['settled_rss_comparison'] = {'initial_median_mib': before, 'final_median_mib': after, 'growth_mib': growth, 'growth_fraction': growth / before if before else None}
                        if before <= 0 or growth > 32 or growth > before * .20:
                            issues.append('Settled RSS growth exceeds the predeclared 32 MiB / 20% budget')
                    phase_start = {phase['phase']: phase['elapsed_seconds'] for phase in phases}
                    before_counters = [c for c in counters if c['kind'] == 'lumen-mem2' and phase_start.get('idle:before', math.inf) <= c['elapsed_seconds'] < phase_start.get('navigation', -1) - 2]
                    after_counters = [c for c in counters if c['kind'] == 'lumen-mem2' and c['elapsed_seconds'] >= phase_start.get('idle:after', math.inf)]
                    if not before_counters or not after_counters:
                        issues.append('Native live-node/style counter comparison is unverified')
                    else:
                        before, after = before_counters[-1]['values'], after_counters[-1]['values']
                        receipt['native_counter_comparison'] = {'initial': before, 'final': after}
                        for key in ('nodes', 'styles'):
                            if key not in before or key not in after or after[key] - before[key] > 50:
                                issues.append(f'Native live {key} increased beyond the predeclared allowance')
                        if after.get('workers') != 0:
                            issues.append('Unexpected live workers after the navigation fixture')
            receipt['samples_sha256'] = sha(samples)
        values = [report['value'] for report in reports]
        if args.scenario == 'scroll':
            completion = [value for value in values if value.get('kind') == 'scroll-complete']
            if len(completion) != 1 or not measured(completion[0].get('events')) or completion[0]['events'] < 10 or not measured(completion[0].get('maxY')) or completion[0]['maxY'] < 500:
                issues.append('Native wheel input did not produce verified page scroll events and movement')
        else:
            loaded = [value for value in values if value.get('kind') == 'loaded']
            if [value.get('step') for value in loaded] != list(range(21)):
                issues.append('Expected 20 consecutive actual page navigations were not observed')
            if any(value.get('checks') != 80 or value.get('documents') != 16 for value in loaded):
                issues.append('Clone/Attr/import checks did not complete on every page')
        receipt['issues'] = issues
        receipt['accepted'] = not issues
        atomic(output / 'receipt.json', receipt)
    print(json.dumps({'receipt': str((output / 'receipt.json').relative_to(ROOT)), 'accepted': not issues, 'issues': issues}))
    return 1 if issues else 0


if __name__ == '__main__':
    def interrupted(signum, _frame):
        signal.signal(signum, signal.SIG_IGN)
        raise KeyboardInterrupt(f'Termination requested via signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scenario', choices=('scroll', 'navigation'), required=True)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--telemetry', default=TELEMETRY)
    parser.add_argument('--telemetry-sha256', default=TELEMETRY_SHA)
    sys.exit(run(parser.parse_args()))

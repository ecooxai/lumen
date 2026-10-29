#!/usr/bin/env python3
"""Run a trusted local fixture through the full browser in an isolated offscreen profile."""
import argparse, csv, functools, http.server, os, pathlib, subprocess, tempfile, threading, time

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--marker', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--timeout', type=float, default=25)
    parser.add_argument('--testdriver', action='store_true')
    args = parser.parse_args()
    fixture = pathlib.Path(args.fixture).resolve()
    output = pathlib.Path(args.output).resolve(); output.mkdir(parents=True, exist_ok=True)
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, format, *values):
            with (output / 'requests.log').open('a') as stream: stream.write((format % values) + '\n')
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(fixture.parent)))
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    marker = 'RESULTS ' + args.marker + ':'
    result = None; peak_rss = 0; peak_cpu = 0; clock_ticks = os.sysconf('SC_CLK_TCK'); page_size = os.sysconf('SC_PAGE_SIZE')
    with tempfile.TemporaryDirectory(prefix='lumen-browser-test-') as profile:
        env = dict(os.environ)
        env.pop('LUMEN_PRE_FILE', None)
        env.update(XDG_DATA_HOME=profile + '/data', XDG_CONFIG_HOME=profile + '/config', XDG_CACHE_HOME=profile + '/cache',
                   LUMEN_NO_SESSION='1', LUMEN_CONSOLE='0', LUMEN_NO_GPU='1', LUMEN_WINDOW='800x640',
                   SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
        if args.testdriver: env['LUMEN_TESTDRIVER'] = '1'
        log_path = output / 'browser.log'
        with log_path.open('wb', buffering=0) as log, (output / 'resources.csv').open('w', newline='') as resources:
            writer = csv.writer(resources); writer.writerow(['elapsed_s', 'cpu_percent', 'rss_mb'])
            process = subprocess.Popen([str(pathlib.Path(args.binary).resolve()), 'http://127.0.0.1:%d/%s' % (server.server_port, fixture.name)], env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=log)
            started = time.monotonic(); next_sample = started; previous = None
            try:
                while time.monotonic() - started < args.timeout:
                    now = time.monotonic()
                    if now >= next_sample:
                        try:
                            stat = pathlib.Path('/proc/%d/stat' % process.pid).read_text().rsplit(')', 1)[1].split()
                            ticks = int(stat[11]) + int(stat[12]); rss = int(stat[21]) * page_size / 1048576
                            cpu = 0 if previous is None else 100 * (ticks - previous[1]) / clock_ticks / (now - previous[0])
                            previous = (now, ticks); peak_rss = max(peak_rss, rss); peak_cpu = max(peak_cpu, cpu)
                            writer.writerow([round(now-started, 3), round(cpu, 3), round(rss, 3)]); resources.flush()
                        except (OSError, ValueError, IndexError): pass
                        next_sample += 1
                    text = log_path.read_text(errors='replace')
                    if result is None:
                        for line in text.splitlines():
                            if marker in line: result = line[line.index(marker):]; break
                        if 'JS prelude failed' in text: result = marker + 'FAIL JavaScript bootstrap failed'
                    if result is not None and now - started >= 1.05: break
                    if process.poll() is not None: break
                    time.sleep(.05)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(4)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()
                server.shutdown(); server.server_close(); thread.join(2)
    if result is None: result = marker + 'FAIL browser exited or fixture timed out'
    print(result)
    print('Resource peak: %.1f%% CPU, %.1f MiB RSS; one-second samples: %s' % (peak_cpu, peak_rss, output / 'resources.csv'))
    return 0 if result.startswith(marker + 'PASS') else 1

if __name__ == '__main__': raise SystemExit(main())

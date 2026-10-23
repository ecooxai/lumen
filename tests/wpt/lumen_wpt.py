#!/usr/bin/env python3
"""Run web-platform-tests (testharness tests) in the real Lumen app and compare the results with Chrome.

The tests are served by WPT's own `wpt serve` (web-platform.test with its subdomains, http/https/ws ports and
Python handlers); Lumen instances resolve those hosts through LUMEN_HOST_MAP and trust the WPT CA through
SSL_CERT_FILE. A small controller hands each instance its next test: every page gets a LUMEN_PRE_FILE that
defines window.__lumenWpt, the local testharnessreport.js posts the finished harness results to it, and the page
then navigates to /lumen/c.py?op=next, a wptserve handler that asks the controller for the next test URL. A
page watchdog reports harness-less or hung pages; a controller deadline kills a stuck instance and restarts it.

usage:
  lumen_wpt.py run [--jobs N] [--limit N] [--restart-every N] PREFIX...   e.g. run /dom/ /url/
  lumen_wpt.py report [PREFIX...]                                          writes report.md and gaps.json
Results go to DATA/lumen.jsonl (one JSON object per test; a rerun skips tests already there, --rerun redoes
them). Chrome's baseline is DATA/chrome-base.json, built from the wpt.fyi raw report of the same WPT revision.
Run `run` through tools/codex_compile.py so the server and every Lumen instance share the host CPU cap.
"""
import argparse, collections, http.server, json, os, re, shutil, signal, subprocess, sys, threading, time, urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WPT = os.environ.get("LUMEN_WPT_ROOT", "/mnt/p3/project/browser/wpt")
DATA = os.environ.get("LUMEN_WPT_DATA", "/mnt/p3/project/browser/wpt-data")
OUT = os.path.join(DATA, "lumen")
TMP = "/tmp/lumen-wpt"
CTRL_PORT = 8790
HOSTS = "web-platform.test=127.0.0.1,not-web-platform.test=127.0.0.1"

# wptserve handler that relays the page's requests to the controller (same origin for the page: no CORS or
# mixed-content questions, and it exists on every WPT origin and port)
HANDLER = '''import os, urllib.request
def main(request, response):
    ctl = os.environ.get("LUMEN_WPT_CTRL", "http://127.0.0.1:%d")
    q = request.url_parts.query
    body = request.body if request.method == "POST" else None
    req = urllib.request.Request(ctl + "/c?" + q, data=body, method=request.method)
    with urllib.request.urlopen(req, timeout=30) as r: text = r.read().decode()
    nocache = [("Cache-Control", "no-store")]
    if "op=next" in q:
        if text: return 302, nocache + [("Location", text)], ""
        return 200, nocache + [("Content-Type", "text/html")], "<!doctype html><title>idle</title>"
    return 200, nocache + [("Content-Type", "text/plain")], text
''' % CTRL_PORT

REPORT_MARK = "/* lumen-wpt */"
REPORT_JS = REPORT_MARK + r'''
(function () {
    var w = window.__lumenWpt;
    if (!w || window.top !== window || typeof add_completion_callback !== "function") return;
    w.harness = true;
    setup({output: false});
    add_completion_callback(function (tests, status) {
        var cut = function (m) { return m == null ? null : String(m).slice(0, 300); };
        w.report({status: ["OK", "ERROR", "TIMEOUT", "PRECONDITION_FAILED"][status.status] || String(status.status),
                  message: cut(status.message),
                  subtests: tests.map(function (t) {
                      return [t.name, ["PASS", "FAIL", "TIMEOUT", "NOTRUN", "PRECONDITION_FAILED"][t.status] || String(t.status), cut(t.message)];
                  })});
    });
})();
'''

VENDOR_MARK = "/* lumen-wpt testdriver */"
# testdriver-vendor.js: click, send_keys and action_sequence become real input through Lumen's __lumenTd(ops)
# (LUMEN_TESTDRIVER), so the events are trusted and go through hit testing, focus and default actions
VENDOR_JS = VENDOR_MARK + r"""
(function () {
    var td = window.__lumenTd, I = window.test_driver_internal;
    if (typeof td !== "function" || !I) return;
    I.in_automation = true;
    var sleep = function (ms) { return new Promise(function (r) { setTimeout(r, ms); }); };
    var cps = function (s) { return Array.from(String(s), function (c) { return c.codePointAt(0); }); };
    var center = function (el) {
        var r = el.getClientRects()[0] || el.getBoundingClientRect();
        var l = Math.max(0, r.left), t = Math.max(0, r.top), ri = Math.min(innerWidth, r.right), b = Math.min(innerHeight, r.bottom);
        return [Math.floor((l + ri) / 2), Math.floor((t + b) / 2)];
    };
    I.click = function (el, c) { return td("move " + c.x + " " + c.y + "\ndown 0\nup 0"); };
    I.send_keys = function (el, keys) {
        if (document.activeElement !== el) el.focus();
        return td(cps(keys).map(function (k) { return "key " + k; }).join("\n"));
    };
    I.action_sequence = async function (sources, ctx) {
        var px = 0, py = 0, n = 0;
        sources.forEach(function (s) { n = Math.max(n, (s.actions || []).length); });
        for (var i = 0; i < n; i++) {
            var ops = [], wait = 0;
            for (var j = 0; j < sources.length; j++) {
                var s = sources[j], a = (s.actions || [])[i];
                if (!a) continue;
                if (a.type === "pause") wait = Math.max(wait, a.duration || 0);
                else if (s.type === "key" && (a.type === "keyDown" || a.type === "keyUp"))
                    ops.push((a.type === "keyDown" ? "key " : "keyup ") + cps(a.value)[0]);
                else if (s.type === "pointer" && a.type === "pointerMove") {
                    var x = a.x || 0, y = a.y || 0, o = a.origin;
                    if (o === "pointer") { x += px; y += py; }
                    else if (o && typeof o === "object") { var c = center(o); x += c[0]; y += c[1]; }
                    px = x; py = y; ops.push("move " + x + " " + y);
                    wait = Math.max(wait, a.duration || 0);
                }
                else if (s.type === "pointer" && (a.type === "pointerDown" || a.type === "pointerUp"))
                    ops.push((a.type === "pointerDown" ? "down " : "up ") + (a.button || 0));
            }
            if (ops.length) await td(ops.join("\n"));
            if (wait) await sleep(wait);
        }
    };
})();
"""

# Evaluated in every top document before its scripts. Captures the builtins it needs first, so tests that
# replace XMLHttpRequest, JSON or setTimeout cannot break the reporting.
PRE_JS = r'''(function () {
    if (window.__lumenWpt || location.pathname === "/lumen/c.py") return;
    var INST = %d, XHR = XMLHttpRequest, stringify = JSON.stringify, st = setTimeout, L = location, rp = L.replace;
    var base = L.protocol + "//" + L.host + "/lumen/c.py?inst=" + INST, sent = false, went = false;
    var go = function () { if (went) return; went = true; rp.call(L, base + "&op=next"); };
    var w = {harness: false, report: function (r) {
        if (sent || window.opener) return; sent = true;
        r.url = L.pathname + L.search;
        var x = new XHR(); x.open("POST", base + "&op=result"); x.onloadend = go;
        try { x.send(stringify(r)); } catch (e) { go(); }
        st(go, 3000);
    }};
    Object.defineProperty(window, "__lumenWpt", {value: w});
    if (window.opener) return;   // popups opened by a test never report or move on
    var long = function () { return !!document.querySelector('meta[name=timeout][content=long]'); };
    addEventListener("load", function () {
        st(function () { if (!w.harness) w.report({status: "ERROR", message: "lumen-wpt: no testharness in the page", subtests: []}); }, 5000);
    });
    st(function () {
        st(function () { w.report({status: "TIMEOUT", message: "lumen-wpt: page watchdog", subtests: []}); }, long() ? 62000 : 7000);
    }, 18000);
})();
'''


def flags_of(url):
    name = url.split("?")[0].split("#")[0].rsplit("/", 1)[-1]
    return set(name.split(".")[1:-1])


def full_url(url):
    f = flags_of(url)
    if "h2" in f: return "https://web-platform.test:9000" + url
    host = ("www." if "www" in f else "") + "web-platform.test"
    if "https" in f or "serviceworker" in f or "serviceworker-module" in f: return "https://%s:8443%s" % (host, url)
    return "http://%s:8000%s" % (host, url)


def load_json(name):
    with open(os.path.join(DATA, name)) as f: return json.load(f)


def select_tests(prefixes, skip):
    base = load_json("chrome-base.json")
    items = load_json("tests.json")["items"]
    urls = sorted(u for u in base if any(u.startswith(p) for p in prefixes) and not any(u.startswith(s) for s in skip))
    urls += [p for p in prefixes if p.startswith("/lumen/")]   # local debug pages
    def long(u):
        it = items.get(u) or items.get(u[1:])
        return isinstance(it, list) and len(it) > 1 and isinstance(it[1], dict) and it[1].get("timeout") == "long"
    return [(u, long(u)) for u in urls]


def done_urls():
    path = os.path.join(OUT, "lumen.jsonl"); got = set()
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                try: got.add(json.loads(line)["url"])
                except (ValueError, KeyError): pass
    return got


def setup_wpt():
    os.makedirs(os.path.join(WPT, "lumen"), exist_ok=True)
    with open(os.path.join(WPT, "lumen", "c.py"), "w") as f: f.write(HANDLER)
    rp = os.path.join(WPT, "resources", "testharnessreport.js")
    src = open(rp).read()
    if REPORT_MARK not in src:
        shutil.copyfile(rp, rp + ".orig")
        with open(rp, "a") as f: f.write("\n" + REPORT_JS)
    vp = os.path.join(WPT, "resources", "testdriver-vendor.js")
    src = open(vp).read()
    if VENDOR_MARK not in src:
        if not os.path.exists(vp + ".orig"): shutil.copyfile(vp, vp + ".orig")
        with open(vp, "a") as f: f.write("\n" + VENDOR_JS)
    os.makedirs(TMP, exist_ok=True)
    ca = os.path.join(TMP, "ca.pem")
    with open(ca, "w") as out:
        out.write(open(os.path.join(WPT, "tools", "certs", "cacert.pem")).read())
        for sysca in ("/etc/ssl/certs/ca-certificates.crt", "/etc/ca-certificates/extracted/tls-ca-bundle.pem"):
            if os.path.exists(sysca): out.write(open(sysca).read()); break
    return ca


class Inst:
    def __init__(self, n): self.n, self.proc, self.cur, self.cur_long, self.t0, self.result, self.fresh, self.count = n, None, None, False, 0, None, False, 0


class Runner:
    def __init__(self, tests, jobs, restart_every, binary, ca):
        self.queue = collections.deque(tests); self.lock = threading.Lock(); self.cv = threading.Condition(self.lock)
        self.insts = [Inst(i) for i in range(jobs)]; self.restart_every = restart_every; self.binary = binary; self.ca = ca
        self.out = open(os.path.join(OUT, "lumen.jsonl"), "a"); self.total = len(tests); self.ndone = 0; self.t_start = time.time()
        self.stop = False; self.counts = collections.Counter()

    # controller side (called from the HTTP server threads)
    def next_url(self, i):
        with self.lock:
            inst = self.insts[i]
            if inst.cur and inst.result is None and not inst.fresh: return ""   # a popup or a navigated page asking
            inst.fresh = False
            if not self.queue or self.stop or inst.count >= self.restart_every:
                inst.cur = None; self.cv.notify_all(); return ""
            inst.cur, inst.cur_long = self.queue.popleft(); inst.result = None; inst.t0 = time.time(); inst.count += 1
            inst.log_off = self.log_size(inst)
            self.cv.notify_all()
            return full_url(inst.cur)

    def post_result(self, i, body):
        try: r = json.loads(body)
        except ValueError: return "bad json"
        with self.lock:
            inst = self.insts[i]
            if not inst.cur or inst.result is not None or r.get("url") != inst.cur: return "ignored"
            self.record(inst, r.get("status", "ERROR"), r.get("message"), r.get("subtests", []))
            return "ok"

    def log_size(self, inst):
        try: return os.path.getsize(os.path.join(TMP, "inst%d.log" % inst.n))
        except OSError: return 0

    def log_lines(self, inst):
        """Error and warning lines Lumen printed while this test ran (first 12)."""
        try:
            with open(os.path.join(TMP, "inst%d.log" % inst.n), "rb") as f:
                f.seek(getattr(inst, "log_off", 0)); text = f.read(400000).decode("utf-8", "replace")
        except OSError: return []
        keep = [l[:300] for l in text.splitlines() if re.search(r"error|warn|uncaught|unhandled|not implemented|unsupported|fail|DBG", l, re.I)]
        return keep[:12]

    def record(self, inst, status, message, subtests):
        inst.result = status; inst.t_res = time.time()
        rec = {"url": inst.cur, "status": status, "message": message, "subtests": subtests, "ms": int((time.time() - inst.t0) * 1000), "inst": inst.n,
               "log": self.log_lines(inst)}
        self.out.write(json.dumps(rec) + "\n"); self.out.flush()
        self.ndone += 1; self.counts[status] += 1; self.cv.notify_all()

    # instance side
    def spawn(self, inst):
        prof = os.path.join(TMP, "prof%d" % inst.n); shutil.rmtree(prof, ignore_errors=True); os.makedirs(prof)
        pre = os.path.join(TMP, "pre%d.js" % inst.n)
        with open(pre, "w") as f: f.write(PRE_JS % inst.n)
        env = dict(os.environ, XDG_DATA_HOME=prof, LUMEN_NO_SESSION="1", LUMEN_HOST_MAP=HOSTS, SSL_CERT_FILE=self.ca,
                   LUMEN_PRE_FILE=pre, LUMEN_WINDOW="800x640", LUMEN_CONSOLE="2", LUMEN_JS_TIMEOUT_MS="60000", LUMEN_TESTDRIVER="1", SDL_VIDEODRIVER=os.environ.get("SDL_VIDEODRIVER", "offscreen"))
        if env["SDL_VIDEODRIVER"] == "offscreen": env["LUMEN_NO_GPU"] = "1"
        log = open(os.path.join(TMP, "inst%d.log" % inst.n), "wb")
        inst.fresh = True; inst.count = 0; inst.cur = None; inst.result = None
        inst.proc = subprocess.Popen([self.binary, "http://web-platform.test:8000/lumen/c.py?op=next&inst=%d" % inst.n],
                                     env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        inst.t0 = time.time()

    def kill(self, inst):
        if inst.proc and inst.proc.poll() is None:
            inst.proc.send_signal(signal.SIGTERM)
            try: inst.proc.wait(5)
            except subprocess.TimeoutExpired: inst.proc.kill(); inst.proc.wait()

    def keep_log(self, inst, why):
        src = os.path.join(TMP, "inst%d.log" % inst.n)
        try:
            tail = open(src, "rb").read()[-20000:]
            with open(os.path.join(OUT, "crash-logs.txt"), "ab") as f:
                f.write(("\n=== %s inst %d %s %s\n" % (time.strftime("%H:%M:%S"), inst.n, why, inst.cur)).encode() + tail)
        except OSError: pass

    def drive(self, inst):
        while True:
            with self.lock:
                if self.stop or (not self.queue and not inst.cur): break
                self.spawn(inst)
            while True:
                with self.lock:
                    self.cv.wait(1.0)
                    if inst.proc.poll() is not None:
                        why = "exit %s" % inst.proc.returncode
                        if inst.cur and inst.result is None: self.record(inst, "CRASH", why, []); self.keep_log(inst, why)
                        elif not inst.fresh and inst.cur is None: pass   # (cannot happen: we never let it exit on its own)
                        inst.cur = None; break
                    limit = 100 if inst.cur_long else 40
                    if inst.cur and inst.result is None and time.time() - inst.t0 > limit:
                        self.record(inst, "TIMEOUT", "lumen-wpt: hard deadline %d s" % limit, []); self.keep_log(inst, "hard timeout")
                        inst.cur = None; break
                    if inst.cur and inst.result is not None and time.time() - inst.t_res > 20:   # done, but never asked for the next test (e.g. reload loop)
                        self.keep_log(inst, "no next request"); inst.cur = None; break
                    if inst.fresh and time.time() - inst.t0 > 60:   # never asked for its first test
                        self.keep_log(inst, "no first request"); inst.cur = None; break
                    if inst.cur is None and not inst.fresh: break      # restart_every reached or queue empty
            self.kill(inst)

    def progress(self):
        while not self.stop:
            time.sleep(30)
            with self.lock:
                el = time.time() - self.t_start; rate = self.ndone / el if el else 0
                eta = (self.total - self.ndone) / rate / 60 if rate else 0
                print("%s %d/%d done (%.2f/s, eta %.0f min) %s" % (time.strftime("%H:%M:%S"), self.ndone, self.total, rate, eta, dict(self.counts)), flush=True)


def serve_controller(runner):
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a): pass
        def reply(self, text):
            b = text.encode(); self.send_response(200); self.send_header("Content-Type", "text/plain"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
        def do_GET(self):
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            self.reply(runner.next_url(int(q["inst"][0])) if q.get("op") == ["next"] else "")
        def do_POST(self):
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            body = self.rfile.read(int(self.headers.get("Content-Length") or 0)).decode("utf-8", "replace")
            self.reply(runner.post_result(int(q["inst"][0]), body) if q.get("op") == ["result"] else "")
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", CTRL_PORT), H); srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def cmd_run(a):
    os.makedirs(OUT, exist_ok=True)
    ca = setup_wpt()
    tests = select_tests(a.prefixes, a.skip)
    if not a.rerun:
        have = done_urls(); tests = [t for t in tests if t[0] not in have]
    if a.limit: tests = tests[:a.limit]
    print("%d tests to run with %d instances" % (len(tests), a.jobs), flush=True)
    if not tests: return 0
    runner = Runner(tests, a.jobs, a.restart_every, a.binary, ca)
    serve_controller(runner)
    slog = open(os.path.join(TMP, "wptserve.log"), "wb")
    srv = subprocess.Popen([os.path.join(WPT, "_venv3", "bin", "python"), os.path.join(WPT, "wpt"), "serve", "--config", os.path.join(OUT, "serve-config.json")],
                           cwd=WPT, env=dict(os.environ, LUMEN_WPT_CTRL="http://127.0.0.1:%d" % CTRL_PORT), stdout=slog, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        time.sleep(a.server_wait)
        threading.Thread(target=runner.progress, daemon=True).start()
        threads = [threading.Thread(target=runner.drive, args=(i,)) for i in runner.insts]
        for t in threads: t.start()
        for t in threads: t.join()
    except KeyboardInterrupt:
        runner.stop = True
        for i in runner.insts: runner.kill(i)
    finally:
        os.killpg(srv.pid, signal.SIGTERM)
        try: srv.wait(10)
        except subprocess.TimeoutExpired: os.killpg(srv.pid, signal.SIGKILL)
    print("finished: %d tests in %.0f s %s" % (runner.ndone, time.time() - runner.t_start, dict(runner.counts)), flush=True)
    return 0


# ---- comparison ----

def score(status, subtests):
    """(pass, total) like wpt.fyi: subtests when there are any, else the harness status as one result."""
    if subtests: return sum(1 for s in subtests if s == "PASS"), len(subtests)
    return (1 if status in ("OK", "PASS") else 0), 1


def cmd_report(a):
    base = load_json("chrome-base.json")
    others = {}
    for name in ("firefox-exp", "safari"):
        try: others[name] = load_json(name + ".json")
        except OSError: pass
    lumen = {}
    with open(os.path.join(OUT, "lumen.jsonl")) as f:
        for line in f:
            r = json.loads(line); lumen[r["url"]] = r     # the last result for a URL wins
    if a.prefixes: lumen = {u: r for u, r in lumen.items() if any(u.startswith(p) for p in a.prefixes)}
    area = lambda u: "/".join(u.split("/")[:3]) + "/" if u.count("/") > 2 and a.deep else "/" + u.split("/")[1] + "/"
    agg = collections.defaultdict(lambda: collections.Counter()); gaps = {}; statuses = collections.Counter()
    for u, r in lumen.items():
        c = base.get(u)
        if not c: continue
        ls = {s[0]: s[1] for s in r["subtests"]}
        lp, lt = score(r["status"], list(ls.values())); cp, ct = score(c["s"], list(c["st"].values()))
        k = agg[area(u)]; t = agg["TOTAL"]
        for d in (k, t):
            d["tests"] += 1; d["lp"] += lp; d["lt"] += lt; d["cp"] += cp; d["ct"] += ct
            d["lnorm"] += lp / lt if lt else 0; d["cnorm"] += cp / ct if ct else 0
            chrome_full = c["s"] == "OK" and all(v == "PASS" for v in c["st"].values())
            lumen_full = r["status"] == "OK" and ls and all(v == "PASS" for v in ls.values()) and set(c["st"]) <= set(ls) if c["st"] else r["status"] == "OK"
            d["cfull"] += chrome_full; d["lfull"] += bool(chrome_full and lumen_full)
            for name in others:
                o = others[name].get(u)
                if o: d[name + "p"] += o["c"][0]; d[name + "t"] += o["c"][1]
        cpass = [n for n, v in c["st"].items() if v == "PASS"] if c["st"] else (["(harness)"] if c["s"] == "OK" else [])
        lpass = set(n for n, v in ls.items() if v == "PASS") if c["st"] else ({"(harness)"} if r["status"] == "OK" else set())
        miss = [n for n in cpass if n not in lpass]
        k["cpass"] += len(cpass); k["lcpass"] += len(cpass) - len(miss); agg["TOTAL"]["cpass"] += len(cpass); agg["TOTAL"]["lcpass"] += len(cpass) - len(miss)
        statuses[r["status"]] += 1
        if miss: gaps[u] = {"lumen": r["status"], "msg": r.get("message"), "missing": len(miss), "of": len(cpass),
                            "first": [[n, ls.get(n, "MISSING"), next((s[2] for s in r["subtests"] if s[0] == n), None)] for n in miss[:5]]}
    pct = lambda p, t: "%.1f%%" % (100.0 * p / t) if t else "-"
    rows = sorted((k for k in agg if k != "TOTAL"), key=lambda k: -(agg[k]["cpass"] - agg[k]["lcpass"]))
    lines = ["# Lumen vs Chrome on web-platform-tests (testharness)", "",
             "WPT revision 0e1c0bce3d. Chrome 158.0.8116.0 (canary, wpt.fyi run on Linux) is the baseline; Firefox 160 / Safari 27 from the same wpt.fyi revision for context.", "",
             "Lumen harness statuses: " + ", ".join("%s %d" % kv for kv in statuses.most_common()), "",
             "| area | tests | Lumen subtests | Chrome subtests | Lumen of Chrome-passing | tests fully passing (Lumen / Chrome) | Lumen score | Chrome score | Firefox | Safari |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for k in ["TOTAL"] + rows:
        d = agg[k]
        lines.append("| %s | %d | %d/%d (%s) | %d/%d (%s) | %d/%d (%s) | %d / %d | %s | %s | %s | %s |" % (
            "**all**" if k == "TOTAL" else k, d["tests"], d["lp"], d["lt"], pct(d["lp"], d["lt"]), d["cp"], d["ct"], pct(d["cp"], d["ct"]),
            d["lcpass"], d["cpass"], pct(d["lcpass"], d["cpass"]), d["lfull"], d["cfull"], pct(d["lnorm"], d["tests"]), pct(d["cnorm"], d["tests"]),
            pct(d["firefox-expp"], d["firefox-expt"]), pct(d["safarip"], d["safarit"])))
    lines += ["", "Score = mean per-test pass fraction (wpt.fyi's normalized view). \"Lumen of Chrome-passing\" counts subtests Chrome passes that Lumen also passes."]
    with open(os.path.join(OUT, "report.md"), "w") as f: f.write("\n".join(lines) + "\n")
    with open(os.path.join(OUT, "gaps.json"), "w") as f: json.dump(dict(sorted(gaps.items(), key=lambda kv: -kv[1]["missing"])), f, indent=0)
    print("\n".join(lines[:9 + min(len(rows), a.top)]))
    print("\n%d tests with Chrome-passing subtests Lumen fails -> %s" % (len(gaps), os.path.join(OUT, "gaps.json")))
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run"); r.add_argument("prefixes", nargs="+"); r.add_argument("--jobs", type=int, default=5)
    r.add_argument("--limit", type=int, default=0); r.add_argument("--restart-every", type=int, default=250)
    r.add_argument("--skip", action="append", default=[]); r.add_argument("--rerun", action="store_true")
    r.add_argument("--binary", default=os.path.join(REPO, "build", "codex-native", "lumen")); r.add_argument("--server-wait", type=float, default=20)
    q = sub.add_parser("report"); q.add_argument("prefixes", nargs="*"); q.add_argument("--deep", action="store_true"); q.add_argument("--top", type=int, default=40)
    a = p.parse_args()
    return cmd_run(a) if a.cmd == "run" else cmd_report(a)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Debug a few WPT tests in Lumen with full assertion messages (lumen_wpt.py cuts them at 300 characters).

usage: wpt_one.py [--all] [--binary PATH] TEST_URL...      e.g. wpt_one.py /css/css-grid/abspos/positioned-grid-descendants-001.html
Needs `wpt serve` on web-platform.test:8000 (lumen_wpt.py run starts one; a leftover one is fine). Prints the
harness status and the failing subtests (all subtests with --all); check-layout messages lose their outerHTML dump.
Lumen runs offscreen; its CPU and RSS are sampled every second and summarised at the end.
"""
import argparse, json, os, re, statistics, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lumen_wpt as W

PRE = r'''(function () {
    if (window.__lumenWpt || window.opener) return;
    var log = console.error.bind(console), stringify = JSON.stringify, sent = false;
    Object.defineProperty(window, "__lumenWpt", {value: {harness: false, report: function (r) {
        if (sent) return; sent = true;
        var cut = function (m) {   // Lumen prints at most 2000 characters of a console line
            m = String(m == null ? "" : m).replace(/^(assert_\w+: )\n<[\s\S]*\n/, "$1");
            return m.length > 1200 ? m.slice(0, 1200) + "..." : m;
        };
        log("LUMENWPT-H " + stringify([r.status, cut(r.message), r.subtests.length]));
        r.subtests.forEach(function (s) { log("LUMENWPT-S " + stringify([String(s[0]).slice(0, 300), s[1], cut(s[2])])); });
        log("LUMENWPT-E");
    }}});
})();
'''
FULL_REPORT = W.REPORT_JS.replace("String(m).slice(0, 300)", "String(m)")


def short(m):
    if not m: return ""
    m = re.sub(r"^(assert_\w+: )\n<.*\n", r"\1", m, flags=re.S)   # check-layout-th's outerHTML dump
    return m.replace("\n", " ")[:400]


def run_one(url, a, ca, pre):
    env = dict(os.environ, XDG_DATA_HOME=tempfile.mkdtemp(prefix="lumen-one-"), LUMEN_NO_SESSION="1", LUMEN_HOST_MAP=W.HOSTS,
               SSL_CERT_FILE=ca, LUMEN_PRE_FILE=pre, LUMEN_WINDOW="800x640", LUMEN_CONSOLE="2", LUMEN_JS_TIMEOUT_MS="60000",
               LUMEN_TESTDRIVER="1", SDL_VIDEODRIVER="offscreen", LUMEN_NO_GPU="1")
    p = subprocess.Popen([a.binary, W.full_url(url)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    os.set_blocking(p.stdout.fileno(), False)
    buf, res, samples, t0 = b"", None, [], time.time()
    while time.time() - t0 < a.timeout and p.poll() is None and res is None:
        time.sleep(1)
        try: buf += p.stdout.read() or b""
        except (BlockingIOError, TypeError): pass
        try:
            cpu, rss = subprocess.check_output(["ps", "-o", "%cpu=,rss=", "-p", str(p.pid)]).split()
            samples.append((float(cpu), int(rss) // 1024))
        except (subprocess.CalledProcessError, ValueError): pass
        if b"LUMENWPT-E" in buf:
            res = {"subtests": []}
            for line in buf.decode("utf-8", "replace").splitlines():
                for tag in ("LUMENWPT-H ", "LUMENWPT-S "):
                    i = line.find(tag)
                    if i < 0: continue
                    try: v = json.loads(line[i + len(tag):])
                    except ValueError: v = [line[i + len(tag):][:200], "?", "unparsed"]
                    if tag == "LUMENWPT-H ": res["status"], res["message"] = v[0], v[1]
                    else: res["subtests"].append(v)
    p.kill(); p.wait()
    text = buf.decode("utf-8", "replace")
    if a.dump:
        with open(a.dump, "a") as f: f.write(text)
    print("== %s" % url)
    if res is None:
        print("  no report (%s)" % ("exit %s" % p.returncode if p.returncode not in (None, -9) else "timeout"))
        for l in text.splitlines()[-a.tail:]: print("  | " + l[:300])
    else:
        sub = res.get("subtests", [])
        npass = sum(1 for s in sub if s[1] == "PASS")
        print("  %s %s  %d/%d pass" % (res.get("status"), short(res.get("message")), npass, len(sub)))
        for name, st, msg in sub:
            if a.all or st != "PASS": print("  %-7s %s: %s" % (st, name[:120], short(msg)))
    errs = [l for l in text.splitlines() if re.search(r"error|uncaught|unhandled", l, re.I) and "LUMENWPT" not in l]
    for l in errs[:a.errors]: print("  ! " + l[:300])
    if samples:
        cpus = [c for c, _ in samples]
        print("  cpu min/median/max %.0f/%.0f/%.0f%%, rss peak %d MB (%d samples)" % (min(cpus), statistics.median(cpus), max(cpus), max(r for _, r in samples), len(samples)))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("urls", nargs="+"); ap.add_argument("--all", action="store_true")
    ap.add_argument("--binary", default=os.path.join(W.REPO, "build", "codex-native", "lumen"))
    ap.add_argument("--timeout", type=float, default=40); ap.add_argument("--tail", type=int, default=15); ap.add_argument("--errors", type=int, default=8)
    ap.add_argument("--dump", help="write the browser's raw stdout/stderr to this file")
    a = ap.parse_args()
    ca = W.setup_wpt()
    rp = os.path.join(W.WPT, "resources", "testharnessreport.js")
    src = open(rp).read()
    if FULL_REPORT not in src:   # full messages here; lumen_wpt.py's cut only matters for the jsonl size
        with open(rp, "w") as f: f.write(src.replace(W.REPORT_JS, FULL_REPORT))
    pre = os.path.join(W.TMP, "pre-one.js")
    with open(pre, "w") as f: f.write(PRE)
    try:
        for u in a.urls: run_one(u, a, ca, pre)
    finally:
        src = open(rp).read()
        with open(rp, "w") as f: f.write(src.replace(FULL_REPORT, W.REPORT_JS))


if __name__ == "__main__":
    main()

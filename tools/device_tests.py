#!/usr/bin/env python3
"""Lumen device test pages: open them on a phone running a debug build and sample Lumen's CPU.

    python3 tools/device_tests.py all                 # before an important commit or a release
    python3 tools/device_tests.py google              # a small fix: only the pages it touches
    python3 tools/device_tests.py --tabs media gfx    # one launch, each page in a tab of its own

Pages: cam (camera capture), screen (screen recording), mic (microphone), webgl (WebGL 1/2, canvas
recording), webgpu (WebGPU compute and render), aquarium (WebGL Aquarium, 10000 fish), chatgpt
(the newest chatgpt.com/c/ bookmark: a long chat), google, youtube, x, utils (the "utils" bookmark,
whose apps menu has the WebGPU Weather World), sitedata (sets cookies and storage and reports
whether the previous load's survived, for site info's Delete data; not in a group), formpost (Enter in
a POST form's field; not in a group), frametarget (a link and forms aimed at a named iframe load
there; not in a group). Groups: media, gfx, sites, all (every page but utils).

By default each page gets a cold start of its own (force-stop, then a launch with only that page)
and its CPU is sampled every second: a local page until it reports "done" and 10 s more (at most
its time), a site for its time. The local pages (tests/device, tests/pages) are
served on 127.0.0.1:8930 through adb reverse and post their numeric results back, which are
printed and saved under --out. Recordings stay on the phone unless a page is given ?up=1.
Bookmark pages open by "bm:TEXT" (LumenActivity TEST_URLS, debug builds only), so their addresses
never leave the phone; this script prints page names, not addresses or page content. It never
types and taps nothing in Lumen; for the screen page it answers Android's own screen-capture
prompt (entire screen, start) and cancels one left open after a page.
"""
import argparse, http.server, os, re, shlex, statistics, subprocess, sys, threading, time, urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG, PORT, LOCAL = "org.lumen.browser", 8930, "http://127.0.0.1:8930"
# name: (address, seconds, settles): pages that settle should be near idle in their last 10 s;
# local pages (tag=) end early once they report done
PAGES = {
    "cam": (LOCAL + "/cam.html?auto=cam&tag=cam", 40, False),
    "screen": (LOCAL + "/screen-recorder.html?auto=1&report=1&tag=screen", 200, False),
    "mic": (LOCAL + "/mic.html?tone=1&ec=0&tag=mic", 40, False),
    "webgl": (LOCAL + "/gfx.html?t=gl1,gl2,anim,rec&tag=webgl", 60, False),
    "webgpu": (LOCAL + "/gfx.html?t=gpu,gpuanim&tag=webgpu", 60, False),
    "aquarium": ("https://webglsamples.org/aquarium/aquarium.html?numFish=10000", 30, False),
    "chatgpt": ("bm:chatgpt.com/c/", 60, True),
    "google": ("https://www.google.com/", 45, True),
    "youtube": ("https://www.youtube.com/", 60, True),
    "x": ("https://x.com/", 60, True),
    "utils": ("bm:utils", 30, False),
    "sitedata": (LOCAL + "/site-data.html?tag=sitedata", 90, False),   # site info's Delete data, tapped by hand meanwhile
    "formpost": (LOCAL + "/form-post.html", 90, False),   # Enter in a POST form's field, typed by hand meanwhile
    "frametarget": (LOCAL + "/frame-target.html", 30, False),   # links and forms aimed at a named iframe
}
GROUPS = {"media": ["cam", "screen", "mic"], "gfx": ["webgl", "webgpu", "aquarium"], "sites": ["chatgpt", "google", "youtube", "x"]}
GROUPS["all"] = GROUPS["media"] + GROUPS["gfx"] + GROUPS["sites"]
IDLE_PCT = 5.0   # a settled page's last-10-s mean above this is reported as a problem (AGENTS.md: 0-3 %)
DONE = re.compile(r"(^|\s)done$|^DONE$|^ERR ", re.I)
CAPTURE_OK = ("Start now", "Start", "Share screen", "Start recording", "Share")
CAPTURE_ALL = ("Entire screen", "Share entire screen", "Record entire screen")
CAPTURE_ONE = ("A single app", "Share one app", "One app", "Record one app")


class Sink(http.server.SimpleHTTPRequestHandler):
    """the test pages, and POST /r?n=TAG (result lines) and /up?n=NAME (a recording, only on ?up=1)"""
    out, done = ".", {}   # done: tag -> when it reported done
    def __init__(self, *a, **k): super().__init__(*a, directory=self.dirs[0], **k)
    def translate_path(self, path):
        name = os.path.basename(urllib.parse.urlsplit(path).path)
        for d in self.dirs:
            if name and os.path.isfile(os.path.join(d, name)): return os.path.join(d, name)
        return os.path.join(self.dirs[0], "-missing-")
    def do_POST(self):
        u = urllib.parse.urlsplit(self.path); q = urllib.parse.parse_qs(u.query)
        tag = re.sub(r"[^A-Za-z0-9_.-]", "", (q.get("n") or ["x"])[0])[:80] or "x"
        body = self.rfile.read(min(int(self.headers.get("Content-Length") or 0), 512 << 20))
        if u.path == "/r":
            text = body.decode("utf-8", "replace")
            with open(os.path.join(self.out, tag + ".txt"), "a") as f: f.write(text + "\n")
            for line in text.splitlines():
                print(f"  [{tag}] {line[:200]}", flush=True)
                if DONE.search(line.strip()): Sink.done.setdefault(tag, time.time())
        elif u.path == "/up":
            with open(os.path.join(self.out, tag), "wb") as f: f.write(body)
            print(f"  [{tag}] saved {len(body)} bytes", flush=True)
        self.send_response(204); self.send_header("Access-Control-Allow-Origin", "*"); self.end_headers()
    def log_message(self, *a): pass


def adb(args, serial, **k):
    return subprocess.run(["adb"] + (["-s", serial] if serial else []) + args, capture_output=True, text=True, **k)


def lumen_pid(serial):
    out = adb(["shell", "pidof", PKG], serial).stdout.split()
    return out[0] if out else None


def ticks(serial, pid):
    """utime + stime in clock ticks (100 a second), and RSS in MB"""
    s = adb(["shell", "cat", f"/proc/{pid}/stat"], serial).stdout
    f = s[s.rfind(")") + 2:].split() if ")" in s else []
    return (int(f[11]) + int(f[12]), int(f[21]) * 4096 / 1048576) if len(f) > 22 else (None, None)


def launch(serial, urls):
    assert all("'" not in u for u in urls)
    adb(["shell", "am", "force-stop", PKG], serial); time.sleep(1)
    cmd = (f"am start -n {PKG}/.LumenActivity --es org.lumen.browser.CONSOLE 2 "
           f"--es org.lumen.browser.TEST_URLS {shlex.quote(' '.join(urls))}")
    return adb(["shell", cmd], serial).returncode == 0


def capture_prompt(serial, accept):
    """Android's screen-capture consent (system UI, not Lumen): accept it for the entire screen, or
    cancel it. Prints only the system buttons it pressed. True when a prompt was showing."""
    focus = re.search(r"mCurrentFocus=[^\n]*", adb(["shell", "dumpsys", "window", "displays"], serial).stdout)
    if not focus or "systemui" not in focus.group(0) and "MediaProjection" not in focus.group(0): return False
    chose = False
    for _ in range(5):
        adb(["shell", "uiautomator", "dump", "/sdcard/lumen-ui.xml"], serial)
        xml = adb(["shell", "cat", "/sdcard/lumen-ui.xml"], serial).stdout
        adb(["shell", "rm", "-f", "/sdcard/lumen-ui.xml"], serial)
        nodes = [n for n in re.findall(r"<node [^>]*>", xml) if 'package="com.android.systemui"' in n]
        def find(labels=(), cls=""):
            for n in nodes:
                t = re.search(r' text="([^"]*)"', n)
                if (not labels or t and t.group(1) in labels) and cls in n:
                    return t.group(1) if t and labels else cls, tuple(map(int, re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', n).groups()))
            return None, None
        texts = " ".join(re.findall(r' text="([^"]*)"', "".join(nodes)))
        prompt = "Lumen" in texts and ("screen" in texts.lower() or "record" in texts.lower())
        if not prompt and not (chose and find(CAPTURE_ALL)[0]): return False   # neither the prompt nor its open choice list
        if not accept: label, box = find(("Cancel",))
        elif chose and find(CAPTURE_ALL)[0]: label, box = find(CAPTURE_ALL)         # the open choice list
        elif not chose and find(CAPTURE_ONE)[0] and find(cls="Spinner")[0]:      # one app chosen: open the list
            label, box = "(screen choice)", find(cls="Spinner")[1]; chose = True
        else: label, box = find(CAPTURE_OK); chose = False
        if not box:
            print("  capture prompt: no known button; system buttons: " + ", ".join(sorted(set(re.findall(r' text="([^"]{1,40})"', "".join(nodes))))))
            return True
        print(f"  capture prompt: pressed {label}", flush=True)
        adb(["shell", "input", "tap", str((box[0] + box[2]) // 2), str((box[1] + box[3]) // 2)], serial)
        time.sleep(1.5)
        if label in CAPTURE_OK or label == "Cancel": return True
    return True


def sample(serial, secs, tag=None, screen=False):
    """per-second CPU % of Lumen for secs (a tagged page: until 10 s after it reports done)"""
    pid = None
    for _ in range(20):
        pid = lumen_pid(serial)
        if pid: break
        time.sleep(0.5)
    if not pid: return [], 0, False
    cpu, rss, (prev, _), t_prev = [], 0, ticks(serial, pid), time.time()
    end = t_prev + secs
    asked = False
    while time.time() < end:
        if tag in Sink.done and time.time() > Sink.done[tag] + 10: break
        if screen and not asked and len(cpu) % 3 == 1: asked = capture_prompt(serial, True)
        time.sleep(max(0, 1 - (time.time() - t_prev)))
        t, mb = ticks(serial, pid); now = time.time()
        if t is None: return cpu, rss, False
        cpu.append((t - prev) / (now - t_prev)); rss = max(rss, mb); prev, t_prev = t, now
    return cpu, rss, True


def report(name, cpu, rss, alive, settles):
    if not cpu:
        print(f"{name}: Lumen did not start"); return False
    last = cpu[-10:]
    line = (f"{name}: CPU mean {statistics.mean(cpu):.1f}% peak {max(cpu):.0f}% last-10-s {statistics.mean(last):.1f}%"
            f"  RSS max {rss:.0f} MB  samples {len(cpu)}")
    ok = alive and not (settles and statistics.mean(last) > IDLE_PCT)
    if not alive: line += "  PROCESS EXITED"
    elif not ok: line += f"  NOT IDLE (> {IDLE_PCT:.0f}% after load)"
    print(line, flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dismiss", action="store_true", help="only cancel a screen-capture prompt left open, then exit")
    ap.add_argument("pages", nargs="*", help="page or group names: " + ", ".join(list(PAGES) + list(GROUPS)))
    ap.add_argument("-s", "--serial", default=os.environ.get("ANDROID_SERIAL"))
    ap.add_argument("--secs", type=int, help="seconds per page (default: each page's own)")
    ap.add_argument("--tabs", action="store_true", help="one launch with every page in a tab of its own")
    ap.add_argument("--out", default="/tmp/lumen-device-tests", help="result files (numbers only)")
    a = ap.parse_args()
    if a.dismiss:
        print("capture prompt cancelled" if capture_prompt(a.serial, False) else "no capture prompt showing")
        return 0
    if not a.pages: ap.error("name at least one page or group")
    names = []
    for n in a.pages:
        for m in GROUPS.get(n, [n]):
            if m not in PAGES: ap.error(f"unknown page {m!r}")
            if m not in names: names.append(m)
    os.makedirs(a.out, exist_ok=True)
    Sink.out, Sink.dirs = a.out, [os.path.join(ROOT, "tests", "device"), os.path.join(ROOT, "tests", "pages")]
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Sink)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    if adb(["reverse", f"tcp:{PORT}", f"tcp:{PORT}"], a.serial).returncode:
        sys.exit("adb reverse failed: is the phone connected?")
    runs = [("tabs:" + ",".join(names), [PAGES[n][0] for n in names], a.secs or 60, False)] if a.tabs else \
           [(n, [PAGES[n][0]], a.secs or PAGES[n][1], PAGES[n][2]) for n in names]
    tag_of = lambda u: (re.search(r"[?&]tag=(\w+)", u) or [None, None])[1]
    failed = []
    try:
        for label, urls, secs, settles in runs:
            tag = tag_of(urls[0]) if len(urls) == 1 else None
            print(f"== {label} (up to {secs} s)", flush=True)
            if tag:
                Sink.done.pop(tag, None)
                if os.path.exists(os.path.join(a.out, tag + ".txt")): os.remove(os.path.join(a.out, tag + ".txt"))
            if not launch(a.serial, urls):
                print(f"{label}: launch failed"); failed.append(label); continue
            cpu, rss, alive = sample(a.serial, secs, tag, PAGES["screen"][0] in urls)
            if not report(label, cpu, rss, alive, settles): failed.append(label)
            if tag and tag not in Sink.done: print(f"{label}: no done report"); failed.append(label)
            if capture_prompt(a.serial, False): print("  capture prompt was still open; cancelled")
    finally:
        adb(["reverse", "--remove", f"tcp:{PORT}"], a.serial); srv.shutdown()
    print(f"results in {a.out}; " + (f"problems: {', '.join(failed)}" if failed else "no problems"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

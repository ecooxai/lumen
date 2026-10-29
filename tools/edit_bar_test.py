#!/usr/bin/env python3
"""Lumen's Android edit bar, tested on a phone without a person or an AI agent driving it.

    python3 tools/edit_bar_test.py [--install] [--serial S] [--only typing,bar,more,clip,hold,showkbd]

Serves tests/device/editbar (a Draft.js editor, as on x.com) on 127.0.0.1:8937 (--port) through adb reverse,
launches it cold, and drives the bar with adb taps. Where things are comes from the page (its first
line, the taps it sees: two calibration taps map screen px to its CSS px) and from Lumen's "LumenBar"
logcat lines (the bar's buttons and the popup's rows, in the same CSS px); what happened comes from the
editor's text, which the page posts after each change. Lumen's CPU and RSS are sampled every second for
the whole run and crashes are looked for in logcat. Typing goes through `adb shell input` (key events,
not the soft keyboard's commitText). Exit status 0 only when every step passed.
"""
import argparse, http.server, json, os, re, statistics, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from device_tests import PKG, adb, lumen_pid, ticks

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE_DIR, PORT = os.path.join(ROOT, "tests", "device", "editbar"), 8937
CUT, COPY, PASTE, ALL, KBD, BKSP, ENTER, UNDO, CLEAR, HIDE, MORE = range(11)
NAMES = ["cut", "copy", "paste", "all", "keyboard", "backspace", "enter", "undo", "clear", "hide", "more"]
STATE = {"downs": [], "models": [], "ready": None}


class Sink(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=PAGE_DIR, **k)
    def do_POST(self):
        o = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        o["rt"] = time.time()
        if o.get("ev") == "down": STATE["downs"].append(o)
        elif o.get("ev") == "model": STATE["models"].append(o)
        elif o.get("ev") == "ready": STATE["ready"] = o
        if "line" in o: STATE["line"], STATE["out"] = o["line"], o["out"]
        self.send_response(204); self.end_headers()
    def log_message(self, *a): pass


class Phone:
    def __init__(self, serial): self.serial, self.map = serial, None
    def sh(self, *args): return adb(["shell"] + [str(a) for a in args], self.serial).stdout
    def tap_screen(self, x, y): self.sh("input", "tap", int(x), int(y))
    def screen(self, cx, cy):
        s, ox, oy = self.map
        return ox + s * cx, oy + s * cy
    def tap(self, cx, cy): self.tap_screen(*self.screen(cx, cy))
    def hold(self, cx, cy, ms):
        x, y = self.screen(cx, cy)
        return subprocess.Popen(["adb"] + (["-s", self.serial] if self.serial else []) + ["shell", "input", "swipe", str(int(x)), str(int(y)), str(int(x)), str(int(y)), str(ms)])
    def keyboard_shown(self):
        m = re.search(r"mInputShown=(true|false)", self.sh("dumpsys", "input_method"))
        return bool(m and m.group(1) == "true")
    def bar_lines(self):
        return [l.split("LumenBar", 1)[1].lstrip(": ").strip() for l in adb(["logcat", "-d", "-s", "LumenBar:I"], self.serial).stdout.splitlines() if "LumenBar" in l]


def wait(cond, secs, step=0.2):
    end = time.time() + secs
    while time.time() < end:
        v = cond()
        if v: return v
        time.sleep(step)
    return cond()


def model(): return STATE["models"][-1]["model"] if STATE["models"] else None


def model_after(n, want=None, secs=4):
    """the editor's text once it changed past the n-th post (and equals want, when given)"""
    def ok():
        if len(STATE["models"]) <= n: return None
        m = STATE["models"][-1]["model"]
        return m if want is None or m == want else None
    wait(ok, secs)
    return model()


def calibrate(ph, w, h):
    """screen px = ox + s * CSS px, from two taps on the page's empty lower part"""
    pts = []
    for fx, fy in ((0.3, 0.55), (0.75, 0.8)):
        n = len(STATE["downs"])
        ph.tap_screen(w * fx, h * fy)
        if not wait(lambda: len(STATE["downs"]) > n, 3): print(f"    no tap seen at {w * fx:.0f},{h * fy:.0f} (screen {w}x{h})"); return False
        d = STATE["downs"][-1]; pts.append((w * fx, h * fy, d["x"], d["y"])); print(f"    tap {w * fx:.0f},{h * fy:.0f} -> {d.get('type')} {d['x']},{d['y']}")
        time.sleep(0.6)
    (sx1, sy1, cx1, cy1), (sx2, sy2, cx2, cy2) = pts
    s = (sx2 - sx1) / (cx2 - cx1)
    ph.map = (s, sx1 - s * cx1, sy1 - s * cy1)
    return abs((sy2 - sy1) / (cy2 - cy1) - s) < 0.05 * s


def parse_bar(line):
    m = re.match(r"bar (\w+) y0=(-?\d+) y1=(-?\d+) x0=(-?\d+)((?: \d+:-?\d+)*)", line)
    if not m: return None
    x, btn = float(m.group(4)), []
    for a, e in re.findall(r"(\d+):(-?\d+)", m.group(5)):
        btn.append((int(a), (x + float(e)) / 2)); x = float(e)
    return {"kind": m.group(1), "y": (float(m.group(2)) + float(m.group(3))) / 2, "btn": btn}


def parse_pop(line):
    m = re.match(r"pop kind=(\d+) x=(-?\d+) y=(-?\d+) w=(\d+) rh=(\d+) acts=([\d,]*)(?: ni=(\d+))?", line)
    if not m: return None
    x, y, w, rh = (float(m.group(i)) for i in (2, 3, 4, 5))
    acts, ni = [int(a) for a in m.group(6).split(",") if a], int(m.group(7) or 0)
    # the first ni actions are icons on the top line, the rest one per row below it
    rows = [(a, x + w / ni * (i + 0.5), y + rh / 2) for i, a in enumerate(acts[:ni])]
    rows += [(a, x + w / 2, y + rh * (i + (1 if ni else 0) + 0.5)) for i, a in enumerate(acts[ni:])]
    return {"kind": int(m.group(1)), "rows": rows}


class Run:
    def __init__(self, ph): self.ph, self.results = ph, []
    def check(self, name, ok, detail=""):
        self.results.append((name, bool(ok), detail))
        print(f"  {'PASS' if ok else 'FAIL'} {name}{': ' + detail if detail else ''}", flush=True)
        return ok
    def bar(self, cx=None, cy=None, tries=3):
        """tap the editor's line (its end, by default) until the edit bar shows; the bar, or None"""
        l = STATE["line"]
        cx = l[2] - 4 if cx is None else cx; cy = (l[1] + l[3]) / 2 if cy is None else cy
        for _ in range(tries):
            n = len(self.ph.bar_lines())
            self.ph.tap(cx, cy)
            got = wait(lambda: [b for b in map(parse_bar, self.ph.bar_lines()[n:]) if b and b["kind"] == "edit"], 2.5)
            if got: return got[-1]
            time.sleep(0.8)   # not a second tap of the first
        return None
    def press(self, b, act):
        x = next((x for a, x in b["btn"] if a == act), None)
        if x is None: return False
        self.ph.tap(x, b["y"]); return True
    def pop(self, n, kind):
        got = wait(lambda: [p for p in map(parse_pop, self.ph.bar_lines()[n:]) if p and p["kind"] == kind], 3)
        return got[-1] if got else None


def steps(r, only):
    ph = r.ph
    if "typing" in only:
        b = r.bar()
        if not r.check("bar shows on a tap, keyboard stays down", b and not ph.keyboard_shown(), b and "buttons " + ",".join(NAMES[a] for a, _ in b["btn"])): return
        r.check("bar buttons", [a for a, _ in b["btn"]][:5] == [KBD, CUT, COPY, PASTE, ALL] and b["btn"][-1][0] == MORE)
        r.press(b, KBD)
        r.check("keyboard button raises the keyboard", wait(ph.keyboard_shown, 3))
        n = len(STATE["models"]); ph.sh("input", "text", "1a")
        r.check("typed 1a once", model_after(n, "1a") == "1a", repr(model()))
        n = len(STATE["models"]); ph.sh("input", "keyevent", "KEYCODE_ENTER"); model_after(n)
        n = len(STATE["models"]); ph.sh("input", "text", "z")
        r.check("Enter starts a line", model_after(n, "1a|z") == "1a|z", repr(model()))
        ph.sh("input", "keyevent", "KEYCODE_BACK")
        r.check("Back hides the keyboard", wait(lambda: not ph.keyboard_shown(), 3))
    if "bar" in only:
        before = model(); b = r.bar()
        n = len(STATE["models"]); r.press(b, ENTER) if b else None
        r.check("bar Enter", model_after(n, before + "|") == before + "|", repr(model()))
        b = r.bar(); n = len(STATE["models"]); r.press(b, BKSP) if b else None
        r.check("bar Backspace", model_after(n, before) == before, repr(model()))
    if "more" in only:
        before = model(); b = r.bar(); k = len(ph.bar_lines())
        r.press(b, MORE) if b else None
        p = r.pop(k, 1)
        if r.check("More opens its list", p, p and "rows " + ",".join(NAMES[a] if a < 100 else f"snippet{a - 100}" for a, _, _ in p["rows"])):
            row = next((row for row in p["rows"] if row[0] == 100), None)
            n = len(STATE["models"]); ph.tap(row[1], row[2])
            r.check("snippet 'yes' + Enter", model_after(n, before + "yes|") == before + "yes|", repr(model()))
    if "clip" in only:
        before = model(); b = r.bar(); r.press(b, COPY) if b else None
        time.sleep(0.8)
        b = r.bar(); k = len(ph.bar_lines())
        x = next((x for a, x in b["btn"] if a == PASTE), None) if b else None
        if x is not None: ph.hold(x, b["y"], 1500).wait()
        p = r.pop(k, 2)
        if r.check("held Paste opens the clipboard history", p and any(a >= 200 for a, _, _ in p["rows"])):
            row = next(row for row in p["rows"] if row[0] == 200)
            n = len(STATE["models"]); ph.tap(row[1], row[2])
            got = model_after(n)
            r.check("history entry pastes", got and len(got) > len(before) and got.replace("|", "").count(before.replace("|", "")) >= 2, repr(got))
    if "hold" in only:
        n0 = len(STATE["models"]); before = model(); b = r.bar()
        x = next((x for a, x in b["btn"] if a == BKSP), None) if b else None
        if x is not None: ph.hold(x, b["y"], 5000).wait()
        time.sleep(0.5)
        changes = [m for m in STATE["models"][n0:] if m["rt"] > 0]
        distinct = len({m["model"] for m in changes})
        r.check("held Backspace repeats (>= 3 deletions in 5 s)", distinct >= 3, f"{distinct} changes, {before!r} -> {model()!r}")
        last = STATE["models"][-1]
        r.check("screen matches the editor's model", last["dom"].rstrip("|") == last["model"].rstrip("|"), f"dom {last['dom']!r}")
    if "showkbd" in only:
        r.check("Show keyboard (menu)", show_keyboard(ph), "")
        o = STATE["out"]; ph.tap((o[0] + o[2]) / 2, (o[1] + o[3]) / 2)
        time.sleep(1)
        r.check("keyboard stays up after a tap elsewhere", ph.keyboard_shown())
        n = len(STATE["models"]); ph.sh("input", "text", "q")
        got = model_after(n)
        r.check("typing goes to the last field", got and got.endswith("q") or (got or "").count("q") > 0, repr(got))
        ph.sh("input", "keyevent", "KEYCODE_BACK")


def ui_dump(ph):
    ph.sh("uiautomator", "dump", "/sdcard/lumen_ui.xml")
    return ph.sh("cat", "/sdcard/lumen_ui.xml")


def node_center(xml, pred):
    for m in re.finditer(r"<node [^>]*>", xml):
        n = m.group(0)
        if pred(n):
            b = re.search(r'bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', n)
            if b: x0, y0, x1, y1 = map(int, b.groups()); return (x0 + x1) / 2, (y0 + y1) / 2
    return None


def show_keyboard(ph):
    """the toolbar's More menu, then its Show keyboard row (scrolling the menu to it)"""
    more = node_center(ui_dump(ph), lambda n: re.search(r'content-desc="(More|Menu|More options)[^"]*"', n) and PKG in n)
    if not more: return False
    ph.tap_screen(*more); time.sleep(1)
    for _ in range(4):
        xml = ui_dump(ph)
        row = node_center(xml, lambda n: 'text="Show keyboard"' in n or 'content-desc="Show keyboard"' in n)
        if row: ph.tap_screen(*row); return wait(ph.keyboard_shown, 3)
        s = node_center(xml, lambda n: 'scrollable="true"' in n and PKG in n)
        if not s: break
        ph.sh("input", "swipe", int(s[0]), int(s[1] + 200), int(s[0]), int(s[1] - 200), 300); time.sleep(0.8)
    return False


def sampler(serial, out, stop):
    pid, last = None, None
    while not stop.is_set():
        p = lumen_pid(serial)
        if p != pid: pid, last = p, None; out["pids"].add(p)
        if p:
            t, rss = ticks(serial, p)
            if t is not None and last is not None: out["cpu"].append((time.time(), t - last[0]))
            if rss: out["rss"].append(rss)
            last = (t, time.time())
        stop.wait(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial"); ap.add_argument("--install", action="store_true")
    ap.add_argument("--only", default="typing,bar,more,clip,hold,showkbd")
    ap.add_argument("--port", type=int, default=PORT)
    a = ap.parse_args(); only = set(a.only.split(","))
    port = a.port
    ph = Phone(a.serial)
    if a.install:
        apk = os.path.join(ROOT, "android/app/build/outputs/apk/debug/app-debug.apk")
        print("install:", adb(["install", "-r", apk], a.serial).stdout.strip().splitlines()[-1:])
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), Sink)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    adb(["reverse", f"tcp:{port}", f"tcp:{port}"], a.serial); adb(["logcat", "-c"], a.serial)
    adb(["shell", "am", "force-stop", PKG], a.serial); time.sleep(1)
    adb(["shell", f"am start -n {PKG}/.LumenActivity --es org.lumen.browser.TEST_URLS 'http://127.0.0.1:{port}/index.html'"], a.serial)
    mon, stop = {"cpu": [], "rss": [], "pids": set()}, threading.Event()
    threading.Thread(target=sampler, args=(a.serial, mon, stop), daemon=True).start()
    r = Run(ph)
    try:
        if r.check("page loads", wait(lambda: STATE["ready"], 25)):
            png = subprocess.run(["adb"] + (["-s", a.serial] if a.serial else []) + ["exec-out", "screencap", "-p"], capture_output=True).stdout
            w, h = int.from_bytes(png[16:20], "big"), int.from_bytes(png[20:24], "big")   # as rotated now
            if r.check("calibration", calibrate(ph, w, h), f"scale {ph.map and round(ph.map[0], 3)}"):
                time.sleep(1); steps(r, only)
        time.sleep(10)   # settle, for the idle figure
    finally:
        stop.set(); adb(["reverse", "--remove", f"tcp:{port}"], a.serial); srv.shutdown()
    pct = [d for _, d in mon["cpu"]]
    last10 = pct[-10:]
    crash = [l for l in adb(["logcat", "-d"], a.serial).stdout.splitlines() if re.search(r"FATAL|Fatal signal|ANR in " + re.escape(PKG), l)]
    if pct: print(f"cpu %: min {min(pct)} median {statistics.median(pct)} max {max(pct)} last-10-s median {statistics.median(last10)}")
    if mon["rss"]: print(f"rss MB: peak {max(mon['rss']):.0f} final {mon['rss'][-1]:.0f}; pids {len(mon['pids'] - {None})}")
    r.check("no crash or ANR", not crash, crash[0][:160] if crash else "")
    r.check("settled idle CPU <= 5%", not last10 or statistics.median(last10) <= 5)
    bad = [n for n, ok, _ in r.results if not ok]
    print(f"{len(r.results) - len(bad)}/{len(r.results)} passed" + (": failed " + "; ".join(bad) if bad else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()

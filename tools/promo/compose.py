#!/usr/bin/env python3
"""Compose the Lumen early-preview promo from clips/*.mp4 + clips/*.ram + bench.json + music.wav."""
import json, os, subprocess, math
from PIL import Image, ImageDraw, ImageFont
D = os.path.expanduser("~/sites/promo"); W, H, FPS = 1280, 960, 30
F = "/System/Library/Fonts/SFNS.ttf"; FR = "/System/Library/Fonts/SFNSRounded.ttf"
def font(s, bold=False):
    f = ImageFont.truetype(F, s)
    try: f.set_variation_by_name("Bold" if bold else "Regular")
    except Exception: pass
    return f
B = json.load(open(f"{D}/bench.json"))
BG1, BG2 = (8, 12, 30), (20, 40, 80)
def bg(t):
    im = Image.new("RGB", (W, H)); d = ImageDraw.Draw(im)
    for y in range(0, H, 4):
        k = y / H; c = tuple(int(BG1[i] + (BG2[i] - BG1[i]) * k) for i in range(3)); d.rectangle([0, y, W, y + 4], fill=c)
    for i in range(40):  # drifting grid dots
        x = (i * 137 + t * 40) % W; y = (i * 91) % H
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(60, 140, 255))
    return im
def ctext(d, y, s, f, fill=(255, 255, 255)):
    w = d.textlength(s, font=f); d.text(((W - w) / 2, y), s, font=f, fill=fill)
def ram_at(samples, t):
    v = None
    for ts, mb in samples:
        if ts <= t: v = mb
        else: break
    return v if v is not None else (samples[0][1] if samples else None)
def badge(im, ram):
    d = ImageDraw.Draw(im, "RGBA")
    d.rounded_rectangle([W - 210, 16, W - 16, 52], 18, fill=(255, 170, 0, 230))
    d.text((W - 196, 22), "PREVIEW VERSION", font=font(20, True), fill=(20, 20, 20))
    if ram is not None:
        s = f"Lumen RAM  {ram} MB  (4 tabs open)"; f = font(34, True); w = d.textlength(s, font=f)
        d.rounded_rectangle([(W - w) / 2 - 24, H / 2 - 32, (W + w) / 2 + 24, H / 2 + 30], 24, fill=(0, 0, 0, 150))
        d.text(((W - w) / 2, H / 2 - 24), s, font=f, fill=(120, 255, 160))
scenes = []  # (kind, seconds, data)
scenes.append(("title", 6, None)); scenes.append(("problem", 5, None)); scenes.append(("chart", 5, None))
for c in json.load(open(f"{D}/clips.json")): scenes.append(("clip", c["dur"], c))
scenes.append(("end", 5, None))
def frame_static(kind, t):
    im = bg(t); d = ImageDraw.Draw(im)
    if kind == "title":
        a = min(1, t / 1.2)
        ctext(d, 300, "Lumen browser", font(110, True), (int(255 * a),) * 3)
        ctext(d, 450, "A browser engine written from scratch in C", font(36), (150, 200, 255))
        d.rounded_rectangle([W / 2 - 330, 540, W / 2 + 330, 600], 30, fill=(255, 170, 0))
        ctext(d, 551, "Now in early preview: this is a preview version", font(30, True), (20, 20, 20))
    elif kind == "problem":
        ctext(d, 280, "Running out of memory?", font(72, True))
        ctext(d, 400, "A few tabs in a typical browser can eat gigabytes of RAM.", font(34), (200, 210, 230))
        ctext(d, 460, "Lumen is built to save your computer's memory.", font(34), (120, 255, 160))
    elif kind == "chart":
        ctext(d, 40, "Lumen vs Chrome (measured on this Mac)", font(44, True))
        rows = B["rows"]; mx = max(max(r["chrome"], r["lumen"]) for r in rows)
        y = 140
        for r in rows:
            d.text((80, y), r["label"], font=font(30, True), fill=(255, 255, 255)); y += 46
            for who, col in (("chrome", (230, 90, 80)), ("lumen", (90, 220, 130))):
                v = r[who]; bw = int(900 * v / mx * min(1, t / 1.5))
                d.rounded_rectangle([200, y, 200 + max(bw, 4), y + 40], 8, fill=col)
                d.text((80, y + 4), "Chrome" if who == "chrome" else "Lumen", font=font(26, who == "lumen"), fill=(220, 220, 220))
                d.text((215 + bw, y + 2), f"{v} {r['unit']}", font=font(30, who == "lumen"), fill=(255, 255, 255)); y += 50
            y += 20
        ctext(d, H - 110, B["summary"], font(44, True), (120, 255, 160))
    elif kind == "end":
        ctext(d, 320, "Lumen browser", font(96, True))
        ctext(d, 450, "Early preview  ·  github.com/ecooxai/lumen", font(36), (150, 200, 255))
        ctext(d, 520, "Download the DMG from GitHub Releases", font(32), (200, 210, 230))
    return im
def clip_frames(c):
    p = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-ss", str(c.get("start", 0)), "-t", str(c["dur"]), "-i", f"{D}/clips/{c['file']}.mp4",
                          "-vf", f"fps={FPS},scale=800:600:force_original_aspect_ratio=decrease,pad=800:600:(ow-iw)/2:(oh-ih)/2", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
    while True:
        b = p.stdout.read(800 * 600 * 3)
        if len(b) < 800 * 600 * 3: break
        yield Image.frombytes("RGB", (800, 600), b)
out = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
                        "-i", f"{D}/music.wav", "-shortest", "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "160k",
                        "-af", "afade=t=out:st=%d:d=2" % (sum(s[1] for s in scenes) - 2), f"{D}/lumen-preview.mp4"], stdin=subprocess.PIPE)
T = 0
for kind, dur, c in scenes:
    if kind != "clip":
        for i in range(int(dur * FPS)):
            im = frame_static(kind, i / FPS); badge(im, None); out.stdin.write(im.tobytes())
    else:
        ram = [tuple(map(float, l.split())) for l in open(f"{D}/clips/{c['file']}.ram") if len(l.split()) == 2]
        ram = [(a, int(b)) for a, b in ram]; n = 0
        for fr in clip_frames(c):
            t = n / FPS; im = bg(T + t); d = ImageDraw.Draw(im, "RGBA")
            ctext(d, 28, c["title"], font(44, True))
            im.paste(fr, ((W - 800) // 2, 110)); d.rectangle([(W - 800) // 2 - 2, 108, (W + 800) // 2 + 1, 711], outline=(90, 160, 255), width=2)
            ctext(d, 740, c["tip"], font(32), (220, 230, 255))
            if c.get("tip2"): ctext(d, 790, c["tip2"], font(28), (150, 200, 255))
            badge(im, ram_at(ram, c.get("start", 0) + t)); out.stdin.write(im.tobytes()); n += 1
        T += dur
    T += 0
out.stdin.close(); out.wait(); print("wrote", f"{D}/lumen-preview.mp4")

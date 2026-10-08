#!/usr/bin/env python3
"""Package build/lumen into dist/Lumen.app and dist/Lumen-<ver>.dmg with Homebrew dylibs bundled."""
import os, re, shutil, subprocess, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VER = sys.argv[1] if len(sys.argv) > 1 else "0.1.0-preview"
IDENTITY = os.environ.get("LUMEN_SIGN_ID", "Lumen Debug Signing")
KEYCHAIN = os.environ.get("LUMEN_KEYCHAIN", os.path.expanduser("~/.lumen-signing/lumen-signing.keychain-db"))
DIST = os.path.join(ROOT, "dist")
APP = os.path.join(DIST, "Lumen.app")
MACOS, FW, RES = (os.path.join(APP, "Contents", d) for d in ("MacOS", "Frameworks", "Resources"))

def run(*a, **k): return subprocess.run(a, check=True, capture_output=True, text=True, **k).stdout

def deps(path):
    out = run("otool", "-L", path).splitlines()[1:]
    return [l.strip().split(" (")[0] for l in out]

def rpaths(path):
    out, rp = run("otool", "-l", path).splitlines(), []
    for i, l in enumerate(out):
        if "LC_RPATH" in l:
            m = re.search(r"path (\S+)", out[i + 2])
            if m: rp.append(m.group(1).replace("@loader_path", os.path.dirname(path)))
    return rp

def resolve(name, src):
    if name.startswith("@rpath/") or name.startswith("@loader_path/"):
        base = name.split("/", 1)[1]
        for d in rpaths(src) + [os.path.dirname(src)] + glob.glob("/opt/homebrew/opt/*/lib") + glob.glob("/opt/homebrew/opt/*/libexec"):
            p = os.path.join(d, base)
            if os.path.exists(p): return os.path.realpath(p)
        return None
    return os.path.realpath(name) if name.startswith("/opt/homebrew") or name.startswith("/usr/local") else None

def main():
    shutil.rmtree(APP, ignore_errors=True)
    for d in (MACOS, FW, RES): os.makedirs(d)
    exe = os.path.join(MACOS, "lumen")
    shutil.copy2(os.path.join(ROOT, "build", "lumen"), exe)
    todo, done = [exe], {}
    while todo:
        f = todo.pop()
        os.chmod(f, 0o755)
        for d in deps(f):
            r = resolve(d, f) if f == exe else resolve(d, done.get(f, f))
            if not r: continue
            base = os.path.basename(d)
            dst = os.path.join(FW, base)
            if not os.path.exists(dst):
                shutil.copy2(r, dst); os.chmod(dst, 0o755)
                run("install_name_tool", "-id", "@rpath/" + base, dst)
                done[dst] = r; todo.append(dst)
            if not d.startswith("@rpath/"): run("install_name_tool", "-change", d, "@rpath/" + base, f)
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", exe)
    with open(os.path.join(APP, "Contents", "Info.plist"), "w") as p:
        p.write(f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>Lumen</string>
<key>CFBundleDisplayName</key><string>Lumen</string>
<key>CFBundleIdentifier</key><string>ai.ecoox.lumen</string>
<key>CFBundleExecutable</key><string>lumen</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>{VER}</string>
<key>CFBundleVersion</key><string>{VER}</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
""")
    sign = ["codesign", "--force", "--timestamp=none", "-s", IDENTITY]
    if os.path.exists(KEYCHAIN): sign += ["--keychain", KEYCHAIN]
    else: sign = ["codesign", "--force", "-s", "-"]
    for f in sorted(glob.glob(os.path.join(FW, "*"))): run(*sign, f)
    run(*sign, APP)
    dmg = os.path.join(DIST, f"Lumen-{VER}.dmg")
    stage = os.path.join(DIST, "dmg-stage")
    shutil.rmtree(stage, ignore_errors=True); os.makedirs(stage)
    shutil.copytree(APP, os.path.join(stage, "Lumen.app"), symlinks=True)
    os.symlink("/Applications", os.path.join(stage, "Applications"))
    if os.path.exists(dmg): os.remove(dmg)
    run("hdiutil", "create", "-volname", "Lumen", "-srcfolder", stage, "-ov", "-format", "UDZO", dmg)
    shutil.rmtree(stage)
    if sign[-1] != "-": run(*sign, dmg)
    print(dmg)

main()

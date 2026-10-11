#!/usr/bin/env python3
"""Build a relocatable Linux tarball: binary + full .so closure + bundled loader.

Produces dist/lumen-<ver>-linux-x86_64.tar.gz.  The package runs on any x86_64
Linux distro (even ones with an older glibc than the build machine) because the
launcher starts the binary through the bundled ld.so with a bundled library
path.  Usage: tools/mklinpkg.py <version> <path-to-lumen-binary>
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tarfile

BREW = os.environ.get("BREW", "/home/linuxbrew/.linuxbrew")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEARCH = (
    [f"{BREW}/opt/glibc/lib", f"{BREW}/lib/gcc/current", f"{BREW}/lib"]
    + sorted(glob.glob(f"{BREW}/opt/*/lib"))
    + sorted(glob.glob(f"{BREW}/opt/*/libexec"))
)
# libraries SDL3/wgpu dlopen at runtime: pull them in even when not linked
EXTRA = [
    "libwayland-client.so.0", "libwayland-egl.so.1", "libwayland-cursor.so.0",
    "libxkbcommon.so.0", "libxkbcommon-x11.so.0",
    "libasound.so.2", "libpulse.so.0", "libpipewire-0.3.so.0",
    "libvulkan.so.1", "libEGL.so.1", "libGL.so.1", "libGLX.so.0", "libOpenGL.so.0",
    "libXcursor.so.1", "libXi.so.6", "libXinerama.so.1", "libXrandr.so.2",
    "libXrender.so.1", "libXss.so.1", "libXxf86vm.so.1", "libXfixes.so.3",
    "libxcb.so.1", "libX11-xcb.so.1", "libxkbfile.so.1", "libxshmfence.so.1",
    "libdrm.so.2", "libgbm.so.1",
]
SKIP = {"linux-vdso.so.1", "ld-linux-x86-64.so.2"}


def elf_deps(path):
    out = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
    needed = re.findall(r"NEEDED.*\[(.+)\]", out)
    rpath = []
    for m in re.findall(r"(?:RUNPATH|RPATH).*\[(.+)\]", out):
        rpath += m.split(":")
    return needed, rpath


def resolve(name, src, rpath):
    dirs = [os.path.dirname(src)] + rpath + SEARCH
    for d in dirs:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return os.path.realpath(p)
    # last resort: whatever the system linker finds (plain system libs only)
    out = subprocess.run(["ldd", src], capture_output=True, text=True).stdout
    m = re.search(re.escape(name) + r"\s*=>\s*(\S+)", out)
    return m.group(1) if m and m.group(1).startswith("/") else None


def main():
    ver, exe = sys.argv[1], os.path.realpath(sys.argv[2])
    name = f"lumen-{ver}-linux-x86_64"
    stage = os.path.join(ROOT, "dist", name)
    shutil.rmtree(stage, ignore_errors=True)
    libd = os.path.join(stage, "lib")
    for d in (libd, os.path.join(stage, "share"), os.path.join(stage, "etc")):
        os.makedirs(d)
    shutil.copy2(exe, os.path.join(stage, "lumen-bin"))

    todo, done = [os.path.join(stage, "lumen-bin")], {}
    while todo:
        f = todo.pop()
        needed, rpath = elf_deps(f)
        for n in needed:
            if n in done or n in SKIP:
                continue
            r = resolve(n, f, rpath)
            if not r:
                print(f"  ! unresolved {n} (needed by {os.path.basename(f)})")
                continue
            dst = os.path.join(libd, n)
            if not os.path.exists(dst):
                shutil.copy2(r, dst)
                todo.append(dst)
            done[n] = r

    for n in EXTRA:
        r = resolve(n, exe, [])
        if r and not os.path.exists(os.path.join(libd, n)):
            shutil.copy2(r, os.path.join(libd, n))
    # closure over the dlopen extras too
    todo = [os.path.join(libd, n) for n in os.listdir(libd)]
    while todo:
        f = todo.pop()
        needed, rpath = elf_deps(f)
        for n in needed:
            if n in done or n in SKIP:
                continue
            r = resolve(n, f, rpath)
            if r:
                dst = os.path.join(libd, n)
                if not os.path.exists(dst):
                    shutil.copy2(r, dst)
                    todo.append(dst)
                done[n] = r

    # bundled glibc loader
    for f in glob.glob(f"{BREW}/opt/glibc/lib/ld-linux*"):
        shutil.copy2(f, libd)

    icu = f"{BREW}/opt/v8/libexec/icudtl.dat"
    shutil.copy2(icu, os.path.join(stage, "share", "icudtl.dat"))
    cert = f"{BREW}/etc/openssl@3/cert.pem"
    if os.path.exists(cert):
        shutil.copy2(cert, os.path.join(stage, "etc", "cert.pem"))

    with open(os.path.join(stage, "etc", "fonts.conf"), "w") as f:
        f.write("""<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <dir prefix="xdg">fonts</dir>
  <cachedir>~/.cache/lumen-fontconfig</cachedir>
</fontconfig>
""")

    with open(os.path.join(stage, "lumen"), "w") as f:
        f.write("""#!/bin/sh
# Lumen launcher: run through the bundled loader + libraries.
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export FONTCONFIG_FILE="$d/etc/fonts.conf"
export LUMEN_ICU_DATA="$d/share/icudtl.dat"
[ -f "$d/etc/cert.pem" ] && export SSL_CERT_FILE="${SSL_CERT_FILE:-$d/etc/cert.pem}"
exec "$d/lib/ld-linux-x86-64.so.2" --library-path "$d/lib" "$d/lumen-bin" "$@"
""")
    os.chmod(os.path.join(stage, "lumen"), 0o755)

    lic = os.path.join(ROOT, "LICENSE")
    if os.path.exists(lic):
        shutil.copy2(lic, stage)
    with open(os.path.join(stage, "README.txt"), "w") as f:
        f.write(f"""Lumen {ver} - Linux x86_64 (X11)

Run:  ./lumen [URL]

Self-contained: the bundled loader and libraries are used, so it runs on
older distros too.  It uses the host's X11 display and real GPU drivers
(Vulkan, then OpenGL).  With no GPU it draws with its own fast CPU raster.

Graphics options:  Settings > Graphics, or environment:
  LUMEN_GPU_BACKEND=auto|vulkan|gl|cpu    LUMEN_GPU_ADAPTER=<n>
  LUMEN_ALLOW_SW_GPU=1  (use software GL/Vulkan anyway)
  LUMEN_GEO=lat,lon     (pin geolocation)
Downloads go to ~/Downloads (or $LUMEN_DOWNLOAD_DIR).
""")

    out = os.path.join(ROOT, "dist", name + ".tar.gz")
    with tarfile.open(out, "w:gz") as t:
        t.add(stage, arcname=name)
    print(f"{out}  ({os.path.getsize(out) / 1e6:.1f} MB, {len(done)} libs bundled)")


if __name__ == "__main__":
    main()

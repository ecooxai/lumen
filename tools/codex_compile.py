import os
import subprocess
import sys


def main(arguments):
    v8_build = bool(arguments and arguments[0] == "--v8")
    if v8_build:
        arguments = arguments[1:]
    if not arguments:
        print("Usage: python3 tools/codex_compile.py [--v8] COMMAND [ARGS...]", file=sys.stderr)
        return 2
    slice_name = "codex-lumen-v8.slice" if v8_build else "codex-lumen-compile.slice"
    quota = 42 * (os.cpu_count() or 1) if v8_build else 140
    command = ["chrt", "--idle", "0", "nice", "-n", "19", "--", *arguments] if v8_build else arguments
    try:
        subprocess.run([
            "systemctl", "--user", "set-property", "--runtime",
            slice_name, f"CPUQuota={quota}%", "CPUQuotaPeriodSec=100ms",
        ], check=True)
        os.execvp("systemd-run", [
            "systemd-run", "--user", "--scope", "--quiet", "--collect",
            f"--slice={slice_name}", "--expand-environment=no",
            "--", *command,
        ])
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Cannot launch CPU-limited compilation: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

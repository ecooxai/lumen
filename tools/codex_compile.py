import os
import subprocess
import sys


def main(arguments):
    if not arguments:
        print("Usage: python3 tools/codex_compile.py COMMAND [ARGS...]", file=sys.stderr)
        return 2
    try:
        subprocess.run([
            "systemctl", "--user", "set-property", "--runtime",
            "codex-lumen-compile.slice", "CPUQuota=140%", "CPUQuotaPeriodSec=100ms",
        ], check=True)
        os.execvp("systemd-run", [
            "systemd-run", "--user", "--scope", "--quiet", "--collect",
            "--slice=codex-lumen-compile.slice", "--expand-environment=no",
            "--", *arguments,
        ])
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Cannot launch CPU-limited compilation: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

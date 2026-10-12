import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("compile_limit", ROOT / "tools/codex_compile.py")
compile_limit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compile_limit)


class CompileLimitTests(unittest.TestCase):
    def test_shared_quota_is_applied_before_starting_command(self):
        events = []
        with patch.object(compile_limit.subprocess, "run", side_effect=lambda *args, **kwargs: events.append((args, kwargs))), \
                patch.object(compile_limit.os, "execvp", side_effect=lambda *args: events.append(args)):
            self.assertEqual(compile_limit.main(["ninja", "-j2"]), 0)
        self.assertEqual(events[0], (([
            "systemctl", "--user", "set-property", "--runtime",
            "codex-lumen-compile.slice", "CPUQuota=140%", "CPUQuotaPeriodSec=100ms",
        ],), {"check": True}))
        self.assertEqual(events[1], ("systemd-run", [
            "systemd-run", "--user", "--scope", "--quiet", "--collect",
            "--slice=codex-lumen-compile.slice", "--expand-environment=no",
            "--", "ninja", "-j2",
        ]))

    def test_arguments_are_not_shell_or_environment_expanded(self):
        arguments = ["compiler", "space in filename.cc", "$HOME", "$(touch unwanted)"]
        with patch.object(compile_limit.subprocess, "run"), patch.object(compile_limit.os, "execvp") as execute:
            compile_limit.main(arguments)
        self.assertEqual(execute.call_args.args[1][-len(arguments):], arguments)

    def test_quota_failure_does_not_start_uncapped_command(self):
        with patch.object(compile_limit.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "systemctl")), \
                patch.object(compile_limit.os, "execvp") as execute:
            self.assertEqual(compile_limit.main(["ninja"]), 1)
        execute.assert_not_called()

    def test_missing_systemctl_does_not_start_command(self):
        with patch.object(compile_limit.subprocess, "run", side_effect=FileNotFoundError("systemctl")), \
                patch.object(compile_limit.os, "execvp") as execute:
            self.assertEqual(compile_limit.main(["ninja"]), 1)
        execute.assert_not_called()

    def test_missing_systemd_run_fails_closed(self):
        with patch.object(compile_limit.subprocess, "run"), \
                patch.object(compile_limit.os, "execvp", side_effect=FileNotFoundError("systemd-run")):
            self.assertEqual(compile_limit.main(["ninja"]), 1)

    def test_empty_command_does_not_change_host_configuration(self):
        with patch.object(compile_limit.subprocess, "run") as configure, \
                patch.object(compile_limit.os, "execvp") as execute:
            self.assertEqual(compile_limit.main([]), 2)
        configure.assert_not_called()
        execute.assert_not_called()


if __name__ == "__main__":
    unittest.main()

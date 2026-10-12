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
    def test_all_builds_share_sixty_percent_host_quota(self):
        for processors, expected in [(8, "480%"), (4, "240%"), (None, "60%")]:
            with self.subTest(processors=processors), \
                    patch.object(compile_limit.os, "cpu_count", return_value=processors), \
                    patch.object(compile_limit.subprocess, "run") as configure, \
                    patch.object(compile_limit.os, "execvp") as execute:
                self.assertEqual(compile_limit.main(["--v8", "ninja", "-j6"]), 0)
            self.assertIn("codex-lumen-compile.slice", configure.call_args.args[0])
            self.assertIn(f"CPUQuota={expected}", configure.call_args.args[0])
            self.assertIn("--slice=codex-lumen-compile.slice", execute.call_args.args[1])
            self.assertEqual(execute.call_args.args[1][-3:], ["--", "ninja", "-j6"])
            self.assertEqual(execute.call_args.args[1][-10:-3], ["--", "chrt", "--idle", "0", "nice", "-n", "39"])

    def test_v8_requires_a_command(self):
        with patch.object(compile_limit.subprocess, "run") as configure, \
                patch.object(compile_limit.os, "execvp") as execute:
            self.assertEqual(compile_limit.main(["--v8"]), 2)
        configure.assert_not_called()
        execute.assert_not_called()

    def test_shared_quota_is_applied_before_starting_command(self):
        events = []
        with patch.object(compile_limit.os, "cpu_count", return_value=8), \
                patch.object(compile_limit.subprocess, "run", side_effect=lambda *args, **kwargs: events.append((args, kwargs))), \
                patch.object(compile_limit.os, "execvp", side_effect=lambda *args: events.append(args)):
            self.assertEqual(compile_limit.main(["ninja", "-j2"]), 0)
        self.assertEqual(events[0], (([
            "systemctl", "--user", "set-property", "--runtime",
            "codex-lumen-compile.slice", "CPUQuota=480%", "CPUQuotaPeriodSec=100ms",
        ],), {"check": True}))
        self.assertEqual(events[1], ("systemd-run", [
            "systemd-run", "--user", "--scope", "--quiet", "--collect",
            "--slice=codex-lumen-compile.slice", "--expand-environment=no",
            "--", "chrt", "--idle", "0", "nice", "-n", "39", "--", "ninja", "-j2",
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

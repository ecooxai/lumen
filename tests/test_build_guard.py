import importlib.util
import os
from pathlib import Path
import signal
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_guard", ROOT / "tools/android/build_guard.py")
guard = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(guard)


class BuildGuardTests(unittest.TestCase):
    def setUp(self):
        self.config = {
            "pid": 100, "start": 500, "uid": 1000,
            "parent_pid": 90, "parent_start": 450,
            "scope": "/cpulimit.slice/codex-lumen-v8-recovery.scope",
            "max_cpu_percent": 320, "ancestor_limits": {"/cpulimit.slice": 200},
            "max_rss_bytes": 8000, "min_available_bytes": 4000,
            "max_compilers": 6,
        }
        self.parent = {"pid": 90, "start": 450, "state": "S"}
        self.rows = [self.row(100, 500, "ninja"), self.row(101, 510, "clang++")]

    def row(self, pid, start, command):
        return {"pid": pid, "start": start, "uid": 1000, "parent": 90 if pid == 100 else 100,
                "state": "T", "rss_bytes": 1000, "command": command,
                "cgroup": self.config["scope"], "threads": [{"policy": 5, "nice": 19}]}

    def evaluate(self, quota="320000 100000", ancestors=None, available=10000):
        return guard.violations(self.config, self.rows, self.parent, quota,
                                ancestors or {"/cpulimit.slice": "200000 100000"}, available)

    def test_stricter_shared_parent_and_stopped_build_are_valid(self):
        self.assertEqual(self.evaluate(), [])
        self.assertEqual(self.evaluate("140000 100000"), [])

    def test_quota_ratio_not_exact_period_is_checked(self):
        self.assertEqual(guard.cpu_percent("160000 50000"), 320)
        self.assertEqual(self.evaluate("160000 50000"), [])
        self.assertIsNone(guard.cpu_percent("max 100000"))
        for value in ("-1 100000", "0 100000", "320000 0", "garbage", "320000 100000 trailing"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                guard.cpu_percent(value)

    def test_unlimited_or_excessive_leaf_fails_closed(self):
        for quota in ("max 100000", "321000 100000"):
            with self.subTest(quota=quota):
                self.assertTrue(self.evaluate(quota))

    def test_host_shared_limit_must_not_be_raised_or_missing(self):
        for quota in ("max 100000", "201000 100000"):
            self.assertTrue(self.evaluate(ancestors={"/cpulimit.slice": quota}))
        self.assertTrue(guard.violations(self.config, self.rows, self.parent,
                                       "320000 100000", {}, 10000))

    def test_pid_reuse_or_lost_owner_fails(self):
        self.rows[0]["start"] += 1
        self.assertTrue(self.evaluate())
        self.rows[0]["start"] -= 1
        self.parent["start"] += 1
        self.assertTrue(self.evaluate())
        self.parent = None
        self.assertTrue(self.evaluate())

    def test_escaped_worker_or_wrong_uid_fails(self):
        self.rows[1]["cgroup"] = "/cpulimit.slice/cpulimit-70.slice"
        self.assertTrue(self.evaluate())
        self.rows[1]["cgroup"] = self.config["scope"]
        self.rows[1]["uid"] = 0
        self.assertTrue(self.evaluate())

    def test_memory_thresholds_are_inclusive(self):
        self.assertTrue(self.evaluate(available=4000))
        self.rows[0]["rss_bytes"] = 7000
        self.assertTrue(self.evaluate())

    def test_all_threads_must_stay_at_lowest_cpu_priority(self):
        for thread in ({"policy": 0, "nice": 19}, {"policy": 5, "nice": 18}):
            self.rows[1]["threads"].append(thread)
            self.assertTrue(self.evaluate())
            self.rows[1]["threads"].pop()

    def test_more_than_six_compilers_fails(self):
        self.rows.extend(self.row(pid, pid + 500, "clang++") for pid in range(102, 108))
        self.assertTrue(self.evaluate())

    def test_input_hash_mismatch_or_missing_file_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "args.gn"
            source.write_text("original")
            expected = guard.file_hash(source)
            self.assertEqual(guard.changed_inputs({str(source): expected}), [])
            source.write_text("changed")
            self.assertEqual(guard.changed_inputs({str(source): expected}), [str(source)])
            source.unlink()
            self.assertEqual(guard.changed_inputs({str(source): expected}), [str(source)])

    def test_freeze_only_expected_scope_and_signal_verified_identities(self):
        with tempfile.TemporaryDirectory() as directory:
            scope = Path(directory)
            (scope / "cgroup.freeze").write_text("0")
            with patch.object(guard, "stop_identity", return_value=True) as stop:
                outcome = guard.freeze_build(scope, self.rows, self.config)
            self.assertEqual((scope / "cgroup.freeze").read_text(), "1")
            self.assertTrue(outcome["frozen"])
            self.assertEqual([call.args[0]["pid"] for call in stop.call_args_list], [100, 101])

    def test_pidfd_stop_never_signals_reused_pid(self):
        row = self.rows[0]
        with patch.object(guard.os, "pidfd_open", return_value=999), \
                patch.object(guard.os, "close") as close, \
                patch.object(guard, "process_info", return_value={**row, "start": 999}), \
                patch.object(guard.signal, "pidfd_send_signal") as send:
            self.assertFalse(guard.stop_identity(row, 1000))
        send.assert_not_called()
        close.assert_called_once_with(999)

    def test_pidfd_stop_signals_only_stop_not_resume(self):
        row = self.rows[0]
        with patch.object(guard.os, "pidfd_open", return_value=999), \
                patch.object(guard.os, "close"), \
                patch.object(guard, "process_info", return_value=row), \
                patch.object(guard.signal, "pidfd_send_signal") as send:
            self.assertTrue(guard.stop_identity(row, 1000))
        send.assert_called_once_with(999, signal.SIGSTOP)

    def test_live_self_process_identity_read(self):
        row = guard.process_info(os.getpid())
        self.assertEqual(row["uid"], os.getuid())
        self.assertEqual(row["pid"], os.getpid())
        self.assertEqual(row["parent"], os.getppid())
        self.assertGreater(row["start"], 0)


if __name__ == "__main__":
    unittest.main()

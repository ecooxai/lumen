from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
COLLECTOR = ROOT / "tools/android/codex_proc_sample.sh"
HOOK = r'''
sample_reads=0
mutate_sample() {
    case "$mutation" in
        exit) rm -rf "$process_dir/task/102" ;;
        missing) rm -f "$process_dir/task/102/stat" ;;
        process) rm -f "$process_dir/stat" ;;
    esac
}
cat() {
    case "$1" in */task/*) mutate_sample ;; esac
    command cat "$@"
}
read() {
    if [ "$sample_reads" -eq 1 ]; then mutate_sample; fi
    sample_reads=$((sample_reads + 1))
    command read "$@"
}
'''


class AndroidProcSampleTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="lumen proc ")
        self.addCleanup(self.directory.cleanup)
        self.process = Path(self.directory.name) / "101"
        self.process.mkdir()
        (self.process / "stat").write_text("101 (browser process) S process\n")
        for thread in (101, 102):
            task = self.process / "task" / str(thread)
            task.mkdir(parents=True)
            (task / "stat").write_text(f"{thread} (browser thread) S thread\n")

    def sample(self, mutation="", hook=False):
        script = (
            f"process_dir={shlex.quote(str(self.process))}\n"
            f"mutation={shlex.quote(mutation)}\n"
            + (HOOK if hook else "")
            + f". {shlex.quote(str(COLLECTOR))}\n"
            + 'lumen_read_process_stats "$process_dir"\n'
        )
        return subprocess.run(
            ["/bin/sh", "-c", script], capture_output=True, text=True, timeout=5
        )

    def test_normal_sample_preserves_process_and_thread_lines(self):
        result = self.sample()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            "101 (browser process) S process",
            "101 (browser thread) S thread",
            "102 (browser thread) S thread",
        ])

    def test_task_exiting_after_enumeration_does_not_lose_sample(self):
        result = self.sample("exit", hook=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout.splitlines(), [
            "101 (browser process) S process", "101 (browser thread) S thread"
        ])

    def test_missing_stat_in_existing_task_is_not_ignored(self):
        result = self.sample("missing", hook=True)
        self.assertNotEqual(result.returncode, 0)

    def test_non_readable_task_stat_is_not_ignored(self):
        stat = self.process / "task/102/stat"
        stat.unlink()
        stat.mkdir()
        self.assertNotEqual(self.sample().returncode, 0)

    def test_missing_process_is_not_ignored(self):
        (self.process / "stat").unlink()
        self.assertNotEqual(self.sample().returncode, 0)

    def test_process_disappearance_during_collection_is_not_ignored(self):
        self.assertNotEqual(self.sample("process", hook=True).returncode, 0)

    def test_missing_thread_telemetry_is_not_accepted(self):
        for stat in (self.process / "task").glob("*/stat"):
            stat.unlink()
            stat.parent.rmdir()
        self.assertNotEqual(self.sample().returncode, 0)

    def test_empty_stat_is_not_accepted(self):
        for relative in ("stat", "task/102/stat"):
            with self.subTest(relative=relative):
                stat = self.process / relative
                saved = stat.read_text()
                stat.write_text("\n")
                self.assertNotEqual(self.sample().returncode, 0)
                stat.write_text(saved)

    def test_partial_stat_read_is_not_accepted(self):
        for relative in ("stat", "task/102/stat"):
            with self.subTest(relative=relative):
                stat = self.process / relative
                saved = stat.read_text()
                stat.write_text(saved.rstrip("\n"))
                self.assertNotEqual(self.sample().returncode, 0)
                stat.write_text(saved)


if __name__ == "__main__":
    unittest.main()

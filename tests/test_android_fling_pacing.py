import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AndroidFlingPacingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        source = (ROOT / "src/app/main.c").read_text()
        end = source.index("        if (!SDL_WaitEventTimeout(&ev, to))")
        start = source.rindex("#ifdef __ANDROID__", 0, end)
        block = source[start:end].replace("a.fling", "state.fling")
        (work / "fling_wait_block.h").write_text(block)
        cls.executable = work / "codex-agent-android-fling-pacing"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-D__ANDROID__", "-fsanitize=undefined,float-cast-overflow",
                        "-fno-sanitize-recover=all", "-I", str(work), "-I", str(ROOT / "src"),
                        str(ROOT / "tests/android/fling_pacing_model.c"),
                        str(ROOT / "src/app/android_fling.c"), "-lm", "-o", str(cls.executable)],
                       check=True, timeout=90)

    def scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_inactive_fling_preserves_idle_and_other_deadlines(self):
        self.scenario("inactive")

    def test_new_frame_waits_only_for_remaining_budget(self):
        self.scenario("remaining")

    def test_late_frame_does_not_add_another_sleep(self):
        self.scenario("late")

    def test_earlier_event_deadline_is_preserved(self):
        self.scenario("earlier")

    def test_fractional_wait_rounds_up_without_busy_polling(self):
        self.scenario("fractional")

    def test_invalid_clock_reaches_cancellation_without_cast_overflow(self):
        self.scenario("invalid")

    def test_fast_frame_work_is_included_in_cadence(self):
        self.scenario("cadence")

    def test_slow_frame_does_not_accumulate_extra_delay(self):
        self.scenario("slow")

    def test_cancellation_restores_normal_idle_wait(self):
        self.scenario("cancel")


if __name__ == "__main__":
    unittest.main()

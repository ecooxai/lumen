import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("LUMEN_TEST_SDL_EVENTS", ROOT / "build/codex-android-deps/SDL/src/events/SDL_events.c"))


class AndroidSdlWaitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        source = SOURCE.read_text()
        start = source.index("bool SDL_WaitEventTimeoutNS(")
        end = source.index("\nstatic bool SDL_CallEventWatchers", start)
        (cls.work / "sdl_wait_function.h").write_text(source[start:end])
        cls.executable = cls.work / "codex-sdl-wait-model"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(cls.work), str(ROOT / "tests/android/sdl_wait_model.c"),
                        "-o", str(cls.executable)], check=True)

    def test_idle_timeout_blocks_without_sentinel_self_wake(self):
        self.run_scenario(0)

    def test_event_interrupts_finite_wait(self):
        self.run_scenario(1)

    def test_event_interrupts_infinite_wait(self):
        self.run_scenario(2)

    def test_queued_event_returns_immediately(self):
        self.run_scenario(3)

    def test_empty_poll_does_not_block(self):
        self.run_scenario(4)

    def test_empty_null_poll_consumes_sentinel(self):
        self.run_scenario(5)

    def test_lifecycle_wake_does_not_lose_next_event(self):
        self.run_scenario(6)

    def test_poll_drains_batch_then_stops_at_sentinel(self):
        self.run_scenario(7)

    def test_repeated_timer_waits_do_not_repeat_maintenance_or_self_wake(self):
        self.run_scenario(8)

    def test_blocking_wait_consumes_prior_poll_sentinel(self):
        self.run_scenario(9)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), str(scenario)], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

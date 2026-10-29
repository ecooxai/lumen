from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build/codex-android-deps/SDL/src/events/SDL_events.c"
MODULE = ROOT / "cmake/AndroidSdlWait.cmake"


class AndroidSdlPatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.source = self.work / "SDL_events.c"
        self.source.write_bytes(SOURCE.read_bytes())
        self.output = self.work / "codex_android_SDL_events.c"
        self.script = self.work / "patch.cmake"
        self.script.write_text(f'include("{MODULE}")\nlumen_android_sdl_wait_source("{self.source}" "{self.output}")\n')

    def configure(self):
        return subprocess.run(["cmake", "-P", str(self.script)], capture_output=True, text=True)

    def test_only_android_wait_changes(self):
        original = self.source.read_bytes()
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        old = "#ifdef SDL_PLATFORM_ANDROID\n    for (;;) {\n        SDL_PumpEventsInternal(true);"
        new = "#ifdef SDL_PLATFORM_ANDROID\n    for (;;) {\n        if (!entry_pumped) SDL_PumpEventsInternal(false);\n        entry_pumped = false;"
        entry = "    // If there isn't a poll sentinel event pending, pump events and add one\n    if (SDL_GetAtomicInt(&SDL_sentinel_pending) == 0) {\n        SDL_PumpEventsInternal(true);\n    }"
        replacement = "#ifdef SDL_PLATFORM_ANDROID\n    bool entry_pumped = false;\n    if (SDL_GetAtomicInt(&SDL_sentinel_pending) == 0) {\n        SDL_PumpEventsInternal(include_sentinel);\n        entry_pumped = true;\n    }\n#else\n" + entry + "\n#endif"
        self.assertEqual(self.output.read_text(), original.decode().replace(entry, replacement).replace(old, new))
        self.assertEqual(self.source.read_bytes(), original)

    def test_reconfigure_keeps_generated_timestamp(self):
        self.assertEqual(self.configure().returncode, 0)
        timestamp = self.output.stat().st_mtime_ns
        self.assertEqual(self.configure().returncode, 0)
        self.assertEqual(self.output.stat().st_mtime_ns, timestamp)

    def test_unreviewed_source_is_rejected(self):
        self.source.write_text(self.source.read_text() + "\n")
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unreviewed SDL event source", result.stderr)
        self.assertFalse(self.output.exists())

    def test_missing_source_is_rejected(self):
        self.source.unlink()
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AndroidGraphicsLifecycleTests(unittest.TestCase):
    def test_synchronous_lifecycle_uses_current_native_surface(self):
        source = (ROOT / "src/app/main.c").read_text()
        signature = "static bool SDLCALL android_graphics_lifecycle("
        self.assertIn(signature, source, "Android graphics lifecycle callback is missing")
        start = source.index(signature)
        end = source.index("\n}\n", start) + 2
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "android_graphics_lifecycle.h").write_text(source[start:end])
            executable = work / "codex-android-graphics-lifecycle"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I", str(work),
                            str(ROOT / "tests/android/graphics_lifecycle_model.c"),
                            "-o", str(executable)], check=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_callback_is_registered_and_removed_and_render_is_guarded(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("SDL_AddEventWatch(android_graphics_lifecycle, &a)", source)
        self.assertIn("SDL_RemoveEventWatch(android_graphics_lifecycle, &a)", source)
        render = source[source.index("static void render(App *a)"):source.index("static bool wheel_scroll")]
        self.assertIn("if (a->android_suspended) return;", render)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = (
    "tap", "drag", "slop", "cancel", "chrome", "multitouch", "identity",
    "invalid", "release_delta", "capacity", "reverse", "independent",
)


class AndroidTouchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="lumen-android-touch-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / "touch-test"
        result = subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "tests/test_android_touch.c"),
            str(ROOT / "src/app/android_touch.c"), "-lm", "-o", str(cls.binary),
        ], capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)


def scenario_test(name):
    def run(self):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    return run


for scenario in SCENARIOS:
    setattr(AndroidTouchTests, f"test_{scenario}", scenario_test(scenario))


class AndroidTouchWiringTests(unittest.TestCase):
    def test_native_fingers_reach_gesture_adapter(self):
        source = (ROOT / "src/app/main.c").read_text()
        for token in ("android_touch_input(&a, &ev)", "SDL_EVENT_FINGER_DOWN",
                      "SDL_EVENT_FINGER_MOTION", "SDL_EVENT_FINGER_UP", "SDL_EVENT_FINGER_CANCELED"):
            with self.subTest(token=token):
                self.assertIn(token, source)

    def test_android_disables_duplicate_synthetic_mouse(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn('SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0")', source)
        self.assertIn("SDL_TOUCH_MOUSEID", source)

    def test_host_cmake_registers_gesture_scenarios(self):
        source = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("add_executable(test_android_touch", source)
        self.assertIn("add_test(NAME android_touch_${scenario}", source)


class AndroidTouchInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "src/app/main.c").read_text()
        signature = "static bool android_touch_input("
        cls.has_adapter = signature in source
        if not cls.has_adapter:
            return
        start = source.index(signature)
        opening = source.index("{", start)
        depth, ending = 1, opening + 1
        while depth:
            depth += (source[ending] == "{") - (source[ending] == "}")
            ending += 1
        cls.directory = tempfile.TemporaryDirectory(prefix="lumen-touch-input-")
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        (work / "touch_input_function.h").write_text(source[start:ending])
        cls.binary = work / "touch-input-test"
        result = subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
            "-I", str(work), str(ROOT / "tests/android/touch_input_model.c"),
            str(ROOT / "src/app/android_touch.c"), str(ROOT / "src/app/android_fling.c"),
            "-lm", "-o", str(cls.binary),
        ], capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)


def input_test(name):
    def run(self):
        self.assertTrue(self.has_adapter, "Missing Android app touch adapter")
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    return run


for scenario in ("tap", "scroll", "synthetic", "cancel", "navigation", "chrome", "bounds", "dom_cancel", "dom_pointer"):
    setattr(AndroidTouchInputTests, f"test_{scenario}", input_test(scenario))


if __name__ == "__main__":
    unittest.main()

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AndroidFlingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        cls.executable = Path(cls.directory.name) / "codex-agent-android-fling"
        implementation = ROOT / "src/app/android_fling.c"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "src"), str(ROOT / "tests/test_android_fling.c"),
                        *([str(implementation)] if implementation.exists() else []),
                        "-lm", "-o", str(cls.executable)], check=True, timeout=90)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def scenario_test(scenario):
    def run(self):
        self.run_scenario(scenario)
    return run


for scenario in ("release", "slow", "hold", "cancel", "invalid", "stall", "pixels", "cadence", "stop", "reverse", "bounded", "independent"):
    setattr(AndroidFlingTests, "test_" + scenario, scenario_test(scenario))


if __name__ == "__main__":
    unittest.main()

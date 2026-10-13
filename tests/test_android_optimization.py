import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AndroidOptimizationTests(unittest.TestCase):
    def flags(self, android=True, enabled=True, configuration="Debug"):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            module = ROOT / "cmake/AndroidOptimization.cmake"
            invocation = (f'include("{module}")\nlumen_android_optimization(probe)\n'
                          if module.exists() else "")
            (work / "probe.c").write_text("int probe(void) { return 42; }\n")
            (work / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\nproject(OptimizationProbe C)\n"
                f"set(ANDROID {'ON' if android else 'OFF'})\n"
                "add_library(probe STATIC probe.c)\n" + invocation)
            subprocess.run(["cmake", "-S", str(work), "-B", str(work / "build"),
                            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                            f"-DCMAKE_C_COMPILER={os.environ.get('CC', 'cc')}",
                            f"-DCMAKE_BUILD_TYPE={configuration}",
                            *([] if enabled else ["-DLUMEN_ANDROID_OPTIMIZED_DEBUG=OFF"])],
                           check=True, capture_output=True, text=True, timeout=90)
            command = json.loads((work / "build/compile_commands.json").read_text())[0]["command"]
            return shlex.split(command)

    def test_android_debug_optimizes_without_losing_symbols_or_assertions(self):
        flags = self.flags()
        self.assertIn("-O2", flags)
        self.assertIn("-g", flags)
        self.assertNotIn("-DNDEBUG", flags)

    def test_developer_can_disable_optimization(self):
        self.assertNotIn("-O2", self.flags(enabled=False))

    def test_desktop_debug_is_unchanged(self):
        self.assertNotIn("-O2", self.flags(android=False))

    def test_release_keeps_existing_optimization(self):
        flags = self.flags(configuration="Release")
        self.assertIn("-O3", flags)
        self.assertNotIn("-O2", flags)

    def test_real_android_targets_use_the_policy(self):
        source = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("include(cmake/AndroidOptimization.cmake)", source)
        self.assertIn("lumen_android_optimization(lumen_base lumen_net lumen_engine main SDL3-shared)", source)


if __name__ == "__main__":
    unittest.main()

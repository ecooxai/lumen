import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class PaintCpuProfileTests(unittest.TestCase):
    def run_scenario(self, scenario, android=True):
        source = (ROOT / "src/app/main.c").read_text()
        signatures = ("static double paint_profile_cpu_ms(", "static PaintFrame paint_profile_begin(",
                      "static void paint_profile_stage(", "static void paint_profile_report(")
        for signature in signatures:
            self.assertTrue(signature in source, "Missing CPU instrumentation: " + signature)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "paint_cpu_functions.h").write_text("\n".join(function(source, name) for name in signatures))
            executable = work / "codex-agent-paint-cpu-profile"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                            *(["-D__ANDROID__"] if android else []), "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                            "-I", str(work), "-I", str(ROOT / "src/app"),
                            str(ROOT / "tests/android/paint_cpu_profile_model.c"),
                            "-o", str(executable)], check=True, timeout=60)
            result = subprocess.run([str(executable), scenario], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_disabled_reads_neither_clock(self):
        self.run_scenario("disabled")

    def test_cpu_time_excludes_simulated_waits(self):
        self.run_scenario("separate")

    def test_initial_clock_error_is_unavailable_not_zero_cpu(self):
        self.run_scenario("initial-error")

    def test_mid_frame_clock_error_discards_partial_cpu_sample(self):
        self.run_scenario("mid-error")

    def test_backward_clock_discards_cpu_sample(self):
        self.run_scenario("backward")

    def test_invalid_timespec_is_unavailable(self):
        self.run_scenario("invalid")

    def test_cpu_and_wall_counts_have_explicit_denominators(self):
        self.run_scenario("counts")

    def test_reset_clears_cpu_sums_and_counts(self):
        self.run_scenario("reset")

    def test_android_report_keeps_wall_and_cpu_lines_distinct(self):
        self.run_scenario("report")

    def test_non_android_keeps_wall_profile_without_cpu_reads(self):
        self.run_scenario("unavailable", android=False)

    def test_render_begins_cpu_sampling_before_first_stage(self):
        source = (ROOT / "src/app/main.c").read_text()
        render = function(source, "static void render(")
        self.assertTrue("PaintFrame profile_frame = paint_profile_begin(t0);" in render,
                        "Render must initialize its CPU timing before measuring stages")
        self.assertLess(render.index("paint_profile_begin(t0)"), render.index("PAINT_PREPARE)"))


if __name__ == "__main__":
    unittest.main()

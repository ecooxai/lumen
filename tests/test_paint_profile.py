import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class PaintProfileTests(unittest.TestCase):
    def run_scenario(self, scenario):
        self.assertTrue((ROOT / "src/app/paint_profile.h").exists(),
                        "Missing opt-in paint-stage timing collector")
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "codex-agent-paint-profile"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11",
                            "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all",
                            "-I", str(ROOT / "src/app"),
                            str(ROOT / "tests/android/paint_profile_model.c"),
                            "-o", str(executable)], check=True, timeout=60)
            result = subprocess.run([str(executable), scenario], capture_output=True,
                                    text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_disabled_collector_does_not_accumulate(self):
        self.run_scenario("disabled")

    def test_collects_full_partial_and_blit_frames(self):
        self.run_scenario("counts")

    def test_reports_only_after_one_second_of_actual_elapsed_time(self):
        self.run_scenario("interval")

    def test_report_labels_wall_time_not_cpu_or_frame_rate(self):
        self.run_scenario("format")

    def test_reset_clears_counts_and_preserves_opt_in(self):
        self.run_scenario("reset")

    def run_app_scenario(self, scenario, platform="android"):
        source = (ROOT / "src/app/main.c").read_text()
        signatures = ("static void paint_profile_configure(",
                      "static void paint_profile_stage(", "static void paint_profile_report(")
        for signature in signatures:
            self.assertIn(signature, source)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "profile_functions.h").write_text("\n".join(function(source, signature) for signature in signatures))
            executable = work / "codex-agent-paint-profile-app"
            defines = ["-D__ANDROID__"] if platform != "desktop" else []
            if platform == "release":
                defines.append("-DNDEBUG")
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11",
                            "-D_POSIX_C_SOURCE=200809L", *defines,
                            "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all",
                            "-I", str(ROOT / "src/app"), "-I", str(work),
                            str(ROOT / "tests/android/paint_profile_app_model.c"),
                            "-o", str(executable)], check=True, timeout=60)
            result = subprocess.run([str(executable), scenario], cwd=work,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_debug_marker_accepts_only_exact_one_and_env_opt_in_is_explicit(self):
        self.run_app_scenario("configure")

    def test_release_ignores_private_debug_marker(self):
        self.run_app_scenario("configure", "release")

    def test_non_android_ignores_private_debug_marker(self):
        self.run_app_scenario("configure", "desktop")

    def test_disabled_stage_does_not_read_clock(self):
        self.run_app_scenario("stage")

    def test_actual_android_report_is_bounded_and_resets_totals(self):
        self.run_app_scenario("report")

    def test_android_private_opt_in_is_debug_only_and_strict(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("static void paint_profile_configure(", source)
        configure = function(source, "static void paint_profile_configure(")
        self.assertIn("#if defined(__ANDROID__) && !defined(NDEBUG)", configure)
        self.assertIn('"paint-profile.enabled"', configure)
        self.assertIn("fclose(", configure)
        startup = function(source, "int main(")
        self.assertLess(startup.index("snprintf(g_pref"), startup.index("paint_profile_configure("))

    def test_android_report_reaches_logcat_without_page_data(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("static void paint_profile_report(", source)
        report = function(source, "static void paint_profile_report(")
        self.assertIn("SDL_Log(", report)
        self.assertIn("paint_profile_format(", report)
        self.assertNotIn("->url", report)
        self.assertNotIn("->title", report)

    def test_render_measures_blit_raster_composition_and_presentation(self):
        source = (ROOT / "src/app/main.c").read_text()
        render = function(source, "static void render(")
        for phase in ("PREPARE", "LAYOUT_DL", "SIGNATURES", "RASTER", "COMPOSE", "PRESENT"):
            self.assertIn("paint_profile_stage(&profile_frame, PAINT_" + phase + ")", render)
        self.assertIn("profile_frame.blit = true", render)
        self.assertLess(render.index("memmove("), render.index("PAINT_RASTER)"))
        self.assertLess(render.index("PAINT_COMPOSE)"), render.index("gpu_present_frame("))
        self.assertLess(render.index("SDL_UpdateWindowSurface("), render.index("PAINT_PRESENT)"))
        self.assertIn("paint_profile_report(&profile_frame, t0, part)", render)


if __name__ == "__main__":
    unittest.main()

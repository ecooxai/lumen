from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "android/annotation/TargetApi.java": "package android.annotation; public @interface TargetApi { int value(); }",
    "android/os/Build.java": "package android.os; public class Build { public static class VERSION { public static int SDK_INT = 35; } }",
    "android/graphics/Insets.java": """
package android.graphics;
public class Insets {
    public final int left, top, right, bottom;
    private Insets(int left, int top, int right, int bottom) {
        this.left = left; this.top = top; this.right = right; this.bottom = bottom;
    }
    public static Insets of(int left, int top, int right, int bottom) { return new Insets(left, top, right, bottom); }
}
""",
    "android/view/DisplayCutout.java": """
package android.view;
import android.graphics.Insets;
public class DisplayCutout {
    private final Insets safe;
    public DisplayCutout(Insets safe) { this.safe = safe; }
    public int getSafeInsetLeft() { return safe.left; }
    public int getSafeInsetTop() { return safe.top; }
    public int getSafeInsetRight() { return safe.right; }
    public int getSafeInsetBottom() { return safe.bottom; }
}
""",
    "android/view/WindowInsetsController.java": """
package android.view;
public class WindowInsetsController {
    public static final int APPEARANCE_LIGHT_STATUS_BARS = 8, APPEARANCE_LIGHT_NAVIGATION_BARS = 16;
    public int appearance, mask;
    public void setSystemBarsAppearance(int appearance, int mask) { this.appearance = appearance; this.mask = mask; }
}
""",
    "android/view/Window.java": """
package android.view;
public class Window {
    public boolean fits = true;
    public int fitCalls;
    public void setDecorFitsSystemWindows(boolean fits) { this.fits = fits; fitCalls++; }
}
""",
    "android/view/View.java": """
package android.view;
public class View {
    public interface OnApplyWindowInsetsListener { WindowInsets onApplyWindowInsets(View view, WindowInsets insets); }
    public OnApplyWindowInsetsListener listener;
    public WindowInsetsController controller = new WindowInsetsController();
    public int left, top, right, bottom, paddingCalls, requests, background;
    public int getPaddingLeft() { return left; }
    public int getPaddingTop() { return top; }
    public int getPaddingRight() { return right; }
    public int getPaddingBottom() { return bottom; }
    public void setPadding(int left, int top, int right, int bottom) {
        this.left = left; this.top = top; this.right = right; this.bottom = bottom; paddingCalls++;
    }
    public void setOnApplyWindowInsetsListener(OnApplyWindowInsetsListener listener) { this.listener = listener; }
    public void requestApplyInsets() { requests++; }
    public void setBackgroundColor(int color) { background = color; }
    public WindowInsetsController getWindowInsetsController() { return controller; }
}
""",
    "android/view/WindowInsets.java": """
package android.view;
import android.graphics.Insets;
public class WindowInsets {
    public static class Type {
        public static int systemBars() { return 1; }
        public static int displayCutout() { return 2; }
        public static int ime() { return 4; }
    }
    public final Insets bars, cutout, keyboard;
    public final boolean keyboardVisible;
    public WindowInsets(Insets bars, Insets cutout, Insets keyboard, boolean keyboardVisible) {
        this.bars = bars; this.cutout = cutout; this.keyboard = keyboard; this.keyboardVisible = keyboardVisible;
    }
    public Insets getInsets(int mask) {
        int left = 0, top = 0, right = 0, bottom = 0;
        Insets[] values = { bars, cutout, keyboard };
        for (int index = 0; index < values.length; index++) {
            if ((mask & (1 << index)) == 0 || (index == 2 && !keyboardVisible)) continue;
            Insets value = values[index];
            if (value == null) continue;
            left = Math.max(left, value.left); top = Math.max(top, value.top);
            right = Math.max(right, value.right); bottom = Math.max(bottom, value.bottom);
        }
        return Insets.of(left, top, right, bottom);
    }
    public boolean isVisible(int mask) { return (mask & Type.ime()) != 0 && keyboardVisible; }
    public int getSystemWindowInsetLeft() { return bars.left; }
    public int getSystemWindowInsetTop() { return bars.top; }
    public int getSystemWindowInsetRight() { return bars.right; }
    public int getSystemWindowInsetBottom() { return bars.bottom; }
    public DisplayCutout getDisplayCutout() { return cutout == null ? null : new DisplayCutout(cutout); }
    private Insets subtract(Insets value, int left, int top, int right, int bottom) {
        if (value == null) return null;
        return Insets.of(Math.max(0, value.left - left), Math.max(0, value.top - top),
                         Math.max(0, value.right - right), Math.max(0, value.bottom - bottom));
    }
    public WindowInsets inset(int left, int top, int right, int bottom) {
        assert android.os.Build.VERSION.SDK_INT >= 29;
        return new WindowInsets(subtract(bars, left, top, right, bottom),
                                subtract(cutout, left, top, right, bottom),
                                subtract(keyboard, left, top, right, bottom), keyboardVisible);
    }
    public WindowInsets replaceSystemWindowInsets(int left, int top, int right, int bottom) {
        return new WindowInsets(Insets.of(left, top, right, bottom), cutout, keyboard, keyboardVisible);
    }
    public WindowInsets consumeDisplayCutout() { return new WindowInsets(bars, null, keyboard, keyboardVisible); }
}
""",
}


class AndroidInsetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        sources = []
        for name, contents in STUBS.items():
            path = cls.work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
            sources.append(str(path))
        sources += [str(ROOT / "android/app/src/main/java/org/lumen/browser/BrowserInsets.java"),
                    str(ROOT / "tests/android/BrowserInsetsTest.java")]
        result = subprocess.run(["javac", "--release", "17", "-d", str(cls.work), *sources],
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_scenario(self, scenario):
        result = subprocess.run(["java", "-ea", "-cp", str(self.work),
                                 "org.lumen.browser.BrowserInsetsTest", scenario],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_portrait_chrome_excludes_system_bars_and_cutout(self):
        self.run_scenario("portrait")

    def test_rotation_replaces_insets_without_accumulating_padding(self):
        self.run_scenario("rotation")

    def test_keyboard_resizes_surface_and_preserves_sdl_visibility(self):
        self.run_scenario("keyboard")

    def test_base_padding_is_preserved(self):
        self.run_scenario("base")

    def test_no_insets_does_not_resize_repeatedly(self):
        self.run_scenario("empty")

    def test_api29_uses_legacy_insets_without_api30_calls(self):
        self.run_scenario("legacy")

    def test_api28_source_build_avoids_api29_inset_method(self):
        self.run_scenario("legacy28")

    def test_legacy_device_without_cutout(self):
        self.run_scenario("legacy-no-cutout")

    def test_controller_can_be_unavailable_until_attachment(self):
        self.run_scenario("unattached")


class AndroidInsetsWiringTests(unittest.TestCase):
    def test_activity_installs_insets_after_sdl_creates_container(self):
        source = (ROOT / "android/app/src/main/java/org/lumen/browser/LumenActivity.java").read_text()
        create = source[source.index("protected void onCreate("):source.index("public void onBackPressed(")]
        self.assertIn("BrowserInsets.install(getWindow(), mLayout)", create)
        self.assertLess(create.index("super.onCreate(state)"), create.index("BrowserInsets.install"))


if __name__ == "__main__":
    unittest.main()

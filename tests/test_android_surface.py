from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "android/content/Context.java": """
package android.content;
import android.hardware.display.DisplayManager;
import android.view.Display;
public class Context {
    public static final String DISPLAY_SERVICE = "display";
    public final DisplayManager manager;
    public final Display display;
    public Context(DisplayManager manager, Display display) { this.manager = manager; this.display = display; }
    public Object getSystemService(String name) { return manager; }
}
""",
    "android/view/Display.java": """
package android.view;
public class Display {
    public final int id;
    public int rotation, reads;
    public boolean valid = true;
    public Display(int id) { this.id = id; }
    public int getDisplayId() { return id; }
    public int getRotation() { reads++; return rotation; }
    public boolean isValid() { return valid; }
}
""",
    "android/view/Surface.java": """
package android.view;
public class Surface { public static final int ROTATION_0 = 0, ROTATION_90 = 1, ROTATION_180 = 2, ROTATION_270 = 3; }
""",
    "android/view/SurfaceHolder.java": "package android.view; public interface SurfaceHolder {}",
    "android/hardware/Sensor.java": "package android.hardware; public class Sensor { public static final int TYPE_ACCELEROMETER = 1; }",
    "android/os/Looper.java": """
package android.os;
public class Looper {
    private static final Looper MAIN = new Looper();
    public static Looper getMainLooper() { return MAIN; }
}
""",
    "android/os/Handler.java": """
package android.os;
public class Handler {
    public final Looper looper;
    public Handler(Looper looper) { this.looper = looper; }
}
""",
    "android/hardware/display/DisplayManager.java": """
package android.hardware.display;
import android.os.Handler;
import android.os.Looper;
public class DisplayManager {
    public interface DisplayListener {
        void onDisplayAdded(int displayId);
        void onDisplayRemoved(int displayId);
        void onDisplayChanged(int displayId);
    }
    public DisplayListener listener;
    public int registers, unregisters;
    public boolean mainLooper;
    public void registerDisplayListener(DisplayListener listener, Handler handler) {
        registers++; this.listener = listener; mainLooper = handler.looper == Looper.getMainLooper();
    }
    public void unregisterDisplayListener(DisplayListener listener) { unregisters++; this.listener = null; }
}
""",
    "org/libsdl/app/SDLSurface.java": """
package org.libsdl.app;
import android.content.Context;
import android.view.Display;
import android.view.SurfaceHolder;
public class SDLSurface {
    protected Display mDisplay;
    public Display attachedDisplay;
    public int sensorCalls, resizeCalls;
    protected SDLSurface(Context context) { mDisplay = context.display; attachedDisplay = context.display; }
    public Display getDisplay() { return attachedDisplay; }
    protected void enableSensor(int sensorType, boolean enabled) { sensorCalls++; }
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { resizeCalls++; }
}
""",
}


class AndroidSurfaceTests(unittest.TestCase):
    def test_real_surface_adapter_against_display_lifecycle_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            sources = []
            for name, contents in STUBS.items():
                path = work / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents)
                sources.append(str(path))
            sources += [str(ROOT / "android/app/src/main/java/org/lumen/browser/BrowserSurface.java"),
                        str(ROOT / "tests/android/BrowserSurfaceTest.java")]
            result = subprocess.run(["javac", "--release", "17", "-d", str(work), *sources],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(["java", "-ea", "-cp", str(work), "org.lumen.browser.BrowserSurfaceTest"],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

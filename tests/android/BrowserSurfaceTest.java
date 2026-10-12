package org.lumen.browser;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.display.DisplayManager;
import android.view.Display;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class BrowserSurfaceTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] arguments) {
        DisplayManager manager = new DisplayManager();
        Display display = new Display(7);
        Context context = new Context(manager, display);
        List<Integer> rotations = new ArrayList<>();
        BrowserSurface surface = new BrowserSurface(context, rotations::add);
        check(manager.registers == 0 && rotations.isEmpty(), "constructor must not start polling");
        surface.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
        surface.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
        check(manager.registers == 1, "duplicate resume registers twice");
        check(manager.mainLooper, "callbacks must use main looper");
        check(surface.sensorCalls == 0, "unused accelerometer was enabled");
        check(rotations.equals(Arrays.asList(0)), "initial orientation missing or duplicated");
        int reads = display.reads;
        for (int index = 0; index < 10; index++) manager.listener.onDisplayChanged(99);
        check(display.reads == reads, "unrelated display caused rotation reads");
        for (int rotation = 1; rotation <= 3; rotation++) {
            display.rotation = rotation;
            manager.listener.onDisplayChanged(7);
            manager.listener.onDisplayChanged(7);
        }
        display.rotation = 0;
        manager.listener.onDisplayChanged(7);
        check(rotations.equals(Arrays.asList(0, 90, 180, 270, 0)), "quarter-turn mapping or deduplication");
        DisplayManager.DisplayListener queuedListener = manager.listener;
        surface.enableSensor(Sensor.TYPE_ACCELEROMETER, false);
        surface.enableSensor(Sensor.TYPE_ACCELEROMETER, false);
        check(manager.unregisters == 1 && surface.sensorCalls == 0, "pause cleanup mismatch");
        display.rotation = 2;
        queuedListener.onDisplayChanged(7);
        check(rotations.size() == 5, "queued callback after pause");
        surface.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
        check(manager.registers == 2 && rotations.get(5) == 180, "resume missed background rotation");
        Display replacement = new Display(8);
        replacement.rotation = 3;
        surface.attachedDisplay = replacement;
        manager.listener.onDisplayAdded(8);
        check(rotations.get(6) == 270, "new surface display not tracked");
        replacement.valid = false;
        replacement.rotation = 1;
        manager.listener.onDisplayRemoved(8);
        check(rotations.size() == 7, "invalid display delivered rotation");
        replacement.valid = true;
        surface.surfaceChanged(null, 0, 800, 600);
        check(surface.resizeCalls == 1 && rotations.get(7) == 90, "resize did not preserve SDL/update rotation");
        surface.enableSensor(4, true);
        surface.enableSensor(4, false);
        check(surface.sensorCalls == 2, "unrelated sensor delegation changed");
        surface.close();
        surface.close();
        check(manager.unregisters == 2, "destroy cleanup is not idempotent");
        replacement.rotation = 2;
        queuedListener.onDisplayChanged(8);
        surface.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
        check(rotations.size() == 8 && manager.registers == 2, "destroyed surface reactivated");
        BrowserSurface detached = new BrowserSurface(context, rotations::add);
        detached.attachedDisplay = null;
        detached.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
        check(rotations.get(8) == 180, "startup display fallback missing");
        detached.close();
        BrowserSurface noManager = new BrowserSurface(new Context(null, display), rotations::add);
        noManager.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
        noManager.surfaceChanged(null, 0, 600, 800);
        noManager.close();
        check(noManager.sensorCalls == 0, "missing display manager enabled sensors");
        System.out.println("Browser surface: lifecycle, four rotations, display changes and no sensor polling PASS");
    }
}

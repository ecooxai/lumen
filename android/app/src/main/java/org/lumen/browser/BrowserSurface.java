package org.lumen.browser;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceHolder;
import java.util.function.IntConsumer;
import org.libsdl.app.SDLSurface;

final class BrowserSurface extends SDLSurface implements DisplayManager.DisplayListener {
    private final DisplayManager displayManager;
    private final IntConsumer rotationChanged;
    private boolean tracking;
    private boolean closed;
    private int lastRotation = -1;

    BrowserSurface(Context context, IntConsumer rotationChanged) {
        super(context);
        setDefaultFocusHighlightEnabled(false);
        displayManager = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);
        this.rotationChanged = rotationChanged;
    }

    @Override
    protected void enableSensor(int sensorType, boolean enabled) {
        if (sensorType != Sensor.TYPE_ACCELEROMETER) {
            super.enableSensor(sensorType, enabled);
            return;
        }
        if (closed || tracking == enabled) return;
        tracking = enabled;
        if (displayManager != null) {
            if (enabled) displayManager.registerDisplayListener(this, new Handler(Looper.getMainLooper()));
            else displayManager.unregisterDisplayListener(this);
        }
        if (enabled) updateRotation();
    }

    private Display currentDisplay() {
        Display display = getDisplay();
        return display != null ? display : mDisplay;
    }

    private void updateRotation() {
        if (!tracking || closed) return;
        Display display = currentDisplay();
        if (display == null || !display.isValid()) return;
        int rotation;
        switch (display.getRotation()) {
            case Surface.ROTATION_90: rotation = 90; break;
            case Surface.ROTATION_180: rotation = 180; break;
            case Surface.ROTATION_270: rotation = 270; break;
            default: rotation = 0; break;
        }
        if (rotation == lastRotation) return;
        lastRotation = rotation;
        rotationChanged.accept(rotation);
    }

    @Override
    public void onDisplayChanged(int displayId) {
        if (!tracking || closed) return;
        Display display = currentDisplay();
        if (display != null && display.getDisplayId() == displayId) updateRotation();
    }

    @Override
    public void onDisplayAdded(int displayId) {
        onDisplayChanged(displayId);
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        onDisplayChanged(displayId);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        super.surfaceChanged(holder, format, width, height);
        updateRotation();
    }

    void close() {
        if (closed) return;
        enableSensor(Sensor.TYPE_ACCELEROMETER, false);
        closed = true;
    }
}

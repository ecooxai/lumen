package org.lumen.browser;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.drawable.Drawable;
import android.os.SystemClock;
import android.view.ActionMode;
import android.view.KeyEvent;
import android.view.KeyboardShortcutGroup;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.SearchEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityEvent;
import android.widget.PopupWindow;
import java.util.List;

/* Touch feedback (Settings): a faint circle under each finger, drawn over a window's views (its
 * decor view's overlay) from the touch down until a moment (0.2 s by default) after the finger
 * lifts. Every window of the app has its own: the browser's, its dialogs' and its popup menus'.
 * Its colour, opacity, size and hide delay come from Settings through style(). */
final class TouchFeedback extends Drawable {
    static volatile boolean enabled = true;
    private static final int POINTERS = 10;
    /* 0xRRGGBB, the fill's opacity in % (the ring has twice that), the diameter in dp, the hide delay */
    private static volatile int rgb = 0x90caf9, opacity = 10, sizeDp = 14;
    private static volatile long lingerMs = 200;

    static void style(int color, int opacityPercent, int diameterDp, int hideMs) {
        rgb = color & 0xffffff;
        opacity = Math.max(1, Math.min(100, opacityPercent));
        sizeDp = Math.max(4, Math.min(120, diameterDp));
        lingerMs = Math.max(0, Math.min(5000, hideMs));
    }

    private final View host;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    /* per circle: the pointer id (-1 unused), where it is, when its finger lifted (0 still down) */
    private final int[] ids = new int[POINTERS];
    private final float[] xs = new float[POINTERS], ys = new float[POINTERS];
    private final long[] lifted = new long[POINTERS];
    private final Runnable expire = this::expire;

    private TouchFeedback(View host) {
        this.host = host;
        density = host.getResources().getDisplayMetrics().density;
        ring.setStyle(Paint.Style.STROKE);
        ring.setStrokeWidth(1f * density);
        java.util.Arrays.fill(ids, -1);
        host.getOverlay().add(this);
    }

    static TouchFeedback on(View host) { return new TouchFeedback(host); }

    /* a dialog's window: called after it is shown */
    static void window(Window window) {
        if (window == null) return;
        TouchFeedback feedback = on(window.getDecorView());
        window.setCallback(new Callback(window.getCallback(), feedback));
    }

    static void popup(PopupWindow popup) {
        final TouchFeedback[] feedback = new TouchFeedback[1];
        popup.setTouchInterceptor((view, event) -> {
            if (feedback[0] == null) feedback[0] = on(view);
            feedback[0].touch(event);
            return false;
        });
    }

    /* an event in the host's coordinates */
    void touch(MotionEvent event) {
        int action = event.getActionMasked();
        if (!enabled) {
            if (action == MotionEvent.ACTION_DOWN) clear();
            return;
        }
        long now = SystemClock.uptimeMillis();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                setBounds(0, 0, host.getWidth(), host.getHeight());
                int index = event.getActionIndex(), slot = slot(event.getPointerId(index), true);
                if (slot >= 0) { xs[slot] = event.getX(index); ys[slot] = event.getY(index); lifted[slot] = 0; }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int slot = slot(event.getPointerId(i), false);
                    if (slot >= 0 && lifted[slot] == 0) { xs[slot] = event.getX(i); ys[slot] = event.getY(i); }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int slot = slot(event.getPointerId(event.getActionIndex()), false);
                if (slot >= 0) { lifted[slot] = now; if (lingerMs == 0) ids[slot] = -1; }
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < POINTERS; i++) if (ids[i] >= 0 && lifted[i] == 0) { lifted[i] = now; if (lingerMs == 0) ids[i] = -1; }
                break;
            default:
                return;
        }
        host.removeCallbacks(expire);
        long next = Long.MAX_VALUE, linger = lingerMs;
        for (int i = 0; i < POINTERS; i++) if (ids[i] >= 0 && lifted[i] != 0) next = Math.min(next, lifted[i] + linger);
        if (next != Long.MAX_VALUE) host.postDelayed(expire, Math.max(1, next - now));
        invalidateSelf();
    }

    /* the circle of a pointer id, a new one if asked for */
    private int slot(int id, boolean make) {
        for (int i = 0; i < POINTERS; i++) if (ids[i] == id) return i;
        if (!make) return -1;
        int free = -1;
        for (int i = 0; i < POINTERS && free < 0; i++) if (ids[i] < 0) free = i;
        for (int i = 0; i < POINTERS && free < 0; i++) if (lifted[i] != 0) free = i;
        if (free >= 0) ids[free] = id;
        return free;
    }

    private void expire() {
        long now = SystemClock.uptimeMillis(), next = Long.MAX_VALUE, linger = lingerMs;
        for (int i = 0; i < POINTERS; i++) {
            if (ids[i] < 0 || lifted[i] == 0) continue;
            if (now - lifted[i] >= linger) ids[i] = -1;
            else next = Math.min(next, lifted[i] + linger);
        }
        if (next != Long.MAX_VALUE) host.postDelayed(expire, Math.max(1, next - now));
        invalidateSelf();
    }

    private void clear() {
        boolean any = false;
        for (int i = 0; i < POINTERS; i++) { any |= ids[i] >= 0; ids[i] = -1; }
        host.removeCallbacks(expire);
        if (any) invalidateSelf();
    }

    @Override public void draw(Canvas canvas) {
        int color = rgb, alpha = opacity;
        float radius = sizeDp * density / 2;
        fill.setColor(Math.round(alpha * 2.55f) << 24 | color);
        ring.setColor(Math.round(Math.min(100, alpha * 2) * 2.55f) << 24 | color);
        for (int i = 0; i < POINTERS; i++) {
            if (ids[i] < 0) continue;
            canvas.drawCircle(xs[i], ys[i], radius, fill);
            canvas.drawCircle(xs[i], ys[i], radius, ring);
        }
    }
    @Override public void setAlpha(int alpha) { }
    @Override public void setColorFilter(ColorFilter filter) { }
    @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }

    /* passes everything to the window's own callback, showing its touches first */
    private static final class Callback implements Window.Callback {
        private final Window.Callback inner;
        private final TouchFeedback feedback;
        Callback(Window.Callback inner, TouchFeedback feedback) { this.inner = inner; this.feedback = feedback; }

        @Override public boolean dispatchTouchEvent(MotionEvent event) { feedback.touch(event); return inner.dispatchTouchEvent(event); }
        @Override public boolean dispatchKeyEvent(KeyEvent event) { return inner.dispatchKeyEvent(event); }
        @Override public boolean dispatchKeyShortcutEvent(KeyEvent event) { return inner.dispatchKeyShortcutEvent(event); }
        @Override public boolean dispatchTrackballEvent(MotionEvent event) { return inner.dispatchTrackballEvent(event); }
        @Override public boolean dispatchGenericMotionEvent(MotionEvent event) { return inner.dispatchGenericMotionEvent(event); }
        @Override public boolean dispatchPopulateAccessibilityEvent(AccessibilityEvent event) { return inner.dispatchPopulateAccessibilityEvent(event); }
        @Override public View onCreatePanelView(int feature) { return inner.onCreatePanelView(feature); }
        @Override public boolean onCreatePanelMenu(int feature, Menu menu) { return inner.onCreatePanelMenu(feature, menu); }
        @Override public boolean onPreparePanel(int feature, View view, Menu menu) { return inner.onPreparePanel(feature, view, menu); }
        @Override public boolean onMenuOpened(int feature, Menu menu) { return inner.onMenuOpened(feature, menu); }
        @Override public boolean onMenuItemSelected(int feature, MenuItem item) { return inner.onMenuItemSelected(feature, item); }
        @Override public void onWindowAttributesChanged(WindowManager.LayoutParams attributes) { inner.onWindowAttributesChanged(attributes); }
        @Override public void onContentChanged() { inner.onContentChanged(); }
        @Override public void onWindowFocusChanged(boolean focus) { inner.onWindowFocusChanged(focus); }
        @Override public void onAttachedToWindow() { inner.onAttachedToWindow(); }
        @Override public void onDetachedFromWindow() { inner.onDetachedFromWindow(); }
        @Override public void onPanelClosed(int feature, Menu menu) { inner.onPanelClosed(feature, menu); }
        @Override public boolean onSearchRequested() { return inner.onSearchRequested(); }
        @Override public boolean onSearchRequested(SearchEvent event) { return inner.onSearchRequested(event); }
        @Override public ActionMode onWindowStartingActionMode(ActionMode.Callback callback) { return inner.onWindowStartingActionMode(callback); }
        @Override public ActionMode onWindowStartingActionMode(ActionMode.Callback callback, int type) { return inner.onWindowStartingActionMode(callback, type); }
        @Override public void onActionModeStarted(ActionMode mode) { inner.onActionModeStarted(mode); }
        @Override public void onActionModeFinished(ActionMode mode) { inner.onActionModeFinished(mode); }
        @Override public void onProvideKeyboardShortcuts(List<KeyboardShortcutGroup> data, Menu menu, int device) { inner.onProvideKeyboardShortcuts(data, menu, device); }
        @Override public void onPointerCaptureChanged(boolean captured) { inner.onPointerCaptureChanged(captured); }
    }
}

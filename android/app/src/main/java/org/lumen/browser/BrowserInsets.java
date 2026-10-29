package org.lumen.browser;

import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;

final class BrowserInsets implements View.OnApplyWindowInsetsListener {
    private final int baseLeft, baseTop, baseRight, baseBottom;

    private BrowserInsets(View container) {
        baseLeft = container.getPaddingLeft();
        baseTop = container.getPaddingTop();
        baseRight = container.getPaddingRight();
        baseBottom = container.getPaddingBottom();
    }

    static void install(Window window, View container) {
        if (Build.VERSION.SDK_INT >= 30) Api30.configure(window);
        container.setBackgroundColor(0xfff6f7f9);
        container.setOnApplyWindowInsetsListener(new BrowserInsets(container));
        container.requestApplyInsets();
    }

    @SuppressWarnings("deprecation")
    static void fullscreen(Window window, boolean on) {
        if (Build.VERSION.SDK_INT >= 30) { Api30.fullscreen(window, on); return; }
        int flags = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
        View decor = window.getDecorView();
        decor.setSystemUiVisibility(on ? decor.getSystemUiVisibility() | flags : decor.getSystemUiVisibility() & ~flags);
    }

    @Override
    public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) return Api30.apply(this, view, insets);
        int left = insets.getSystemWindowInsetLeft();
        int top = insets.getSystemWindowInsetTop();
        int right = insets.getSystemWindowInsetRight();
        int bottom = insets.getSystemWindowInsetBottom();
        DisplayCutout cutout = insets.getDisplayCutout();
        if (cutout != null) {
            left = Math.max(left, cutout.getSafeInsetLeft());
            top = Math.max(top, cutout.getSafeInsetTop());
            right = Math.max(right, cutout.getSafeInsetRight());
            bottom = Math.max(bottom, cutout.getSafeInsetBottom());
        }
        return apply(view, insets, left, top, right, bottom);
    }

    private WindowInsets apply(View view, WindowInsets insets, int left, int top, int right, int bottom) {
        left += baseLeft; top += baseTop; right += baseRight; bottom += baseBottom;
        if (view.getPaddingLeft() != left || view.getPaddingTop() != top ||
            view.getPaddingRight() != right || view.getPaddingBottom() != bottom) {
            view.setPadding(left, top, right, bottom);
        }
        if (Build.VERSION.SDK_INT >= 29) return Api29.inset(insets, left, top, right, bottom);
        return insets.replaceSystemWindowInsets(0, 0, 0, 0).consumeDisplayCutout();
    }

    @android.annotation.TargetApi(29)
    private static final class Api29 {
        static WindowInsets inset(WindowInsets insets, int left, int top, int right, int bottom) {
            return insets.inset(left, top, right, bottom);
        }
    }

    @android.annotation.TargetApi(30)
    private static final class Api30 {
        static void configure(Window window) {
            window.setDecorFitsSystemWindows(false);
        }

        static void fullscreen(Window window, boolean on) {
            android.view.WindowInsetsController controller = window.getInsetsController();
            if (controller == null) return;
            if (on) {
                controller.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            } else {
                controller.show(WindowInsets.Type.systemBars());
            }
        }

        static WindowInsets apply(BrowserInsets adapter, View view, WindowInsets insets) {
            android.graphics.Insets safe = insets.getInsets(WindowInsets.Type.systemBars() |
                WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
            android.view.WindowInsetsController controller = view.getWindowInsetsController();
            if (controller != null) {
                int appearance = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS |
                    android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                controller.setSystemBarsAppearance(appearance, appearance);
            }
            return adapter.apply(view, insets, safe.left, safe.top, safe.right, safe.bottom);
        }
    }
}

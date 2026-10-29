package org.lumen.browser;

import android.graphics.Insets;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;

public final class BrowserInsetsTest {
    private static final Insets NONE = Insets.of(0, 0, 0, 0);

    private static void padding(View view, int left, int top, int right, int bottom) {
        assert view.left == left && view.top == top && view.right == right && view.bottom == bottom
            : "padding=" + view.left + "," + view.top + "," + view.right + "," + view.bottom;
    }

    private static WindowInsets apply(View view, Insets bars, Insets cutout, Insets keyboard, boolean shown) {
        return view.listener.onApplyWindowInsets(view, new WindowInsets(bars, cutout, keyboard, shown));
    }

    public static void main(String[] arguments) {
        String scenario = arguments[0];
        Build.VERSION.SDK_INT = scenario.equals("legacy28") ? 28 : scenario.startsWith("legacy") ? 29 : 35;
        View view = new View();
        Window window = new Window();
        if (scenario.equals("base")) view.setPadding(3, 5, 7, 11);
        if (scenario.equals("unattached")) view.controller = null;
        BrowserInsets.install(window, view);
        assert view.listener != null && view.requests == 1;
        assert window.fitCalls == (Build.VERSION.SDK_INT >= 30 ? 1 : 0);
        assert window.fits == (Build.VERSION.SDK_INT < 30);
        if (scenario.equals("empty") || scenario.equals("unattached")) {
            apply(view, NONE, NONE, NONE, false);
            int calls = view.paddingCalls;
            apply(view, NONE, NONE, NONE, false);
            assert calls == view.paddingCalls;
            padding(view, 0, 0, 0, 0);
        } else if (scenario.equals("base")) {
            apply(view, Insets.of(0, 24, 0, 30), NONE, NONE, false);
            padding(view, 3, 29, 7, 41);
            apply(view, Insets.of(0, 24, 0, 30), NONE, NONE, false);
            padding(view, 3, 29, 7, 41);
            apply(view, NONE, NONE, NONE, false);
            padding(view, 3, 5, 7, 11);
        } else if (scenario.startsWith("legacy")) {
            int left = scenario.equals("legacy-no-cutout") ? 0 : 50;
            WindowInsets child = apply(view, Insets.of(0, 24, 0, 48), left == 0 ? null : Insets.of(left, 0, 0, 0), NONE, false);
            padding(view, left, 24, 0, 48);
            assert child.getSystemWindowInsetTop() == 0;
            assert child.getDisplayCutout() == null || child.getDisplayCutout().getSafeInsetLeft() == 0;
            apply(view, NONE, NONE, NONE, false);
            padding(view, 0, 0, 0, 0);
        } else if (scenario.equals("rotation")) {
            apply(view, Insets.of(0, 24, 0, 30), Insets.of(0, 80, 0, 0), NONE, false);
            apply(view, Insets.of(0, 24, 30, 0), Insets.of(80, 0, 0, 0), NONE, false);
            padding(view, 80, 24, 30, 0);
            int calls = view.paddingCalls;
            apply(view, Insets.of(0, 24, 30, 0), Insets.of(80, 0, 0, 0), NONE, false);
            assert view.paddingCalls == calls;
            apply(view, Insets.of(0, 24, 0, 30), Insets.of(0, 0, 80, 0), NONE, false);
            padding(view, 0, 24, 80, 30);
        } else if (scenario.equals("keyboard")) {
            apply(view, Insets.of(0, 80, 0, 30), NONE, NONE, false);
            WindowInsets child = apply(view, Insets.of(0, 80, 0, 30), NONE, Insets.of(0, 0, 0, 850), true);
            padding(view, 0, 80, 0, 850);
            assert child.isVisible(WindowInsets.Type.ime());
            Insets remaining = child.getInsets(WindowInsets.Type.ime() | WindowInsets.Type.systemBars());
            assert remaining.top == 0 && remaining.bottom == 0;
            apply(view, Insets.of(0, 80, 0, 30), NONE, Insets.of(0, 0, 0, 850), false);
            padding(view, 0, 80, 0, 30);
        } else if (scenario.equals("portrait")) {
            WindowInsets child = apply(view, Insets.of(0, 24, 0, 30), Insets.of(0, 80, 0, 0), NONE, false);
            padding(view, 0, 80, 0, 30);
            Insets remaining = child.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            assert remaining.top == 0 && remaining.bottom == 0;
            assert !child.isVisible(WindowInsets.Type.ime());
        } else {
            throw new AssertionError("Unknown scenario " + scenario);
        }
        if (Build.VERSION.SDK_INT >= 30 && view.controller != null) {
            assert view.controller.appearance == 24 && view.controller.mask == 24;
            assert view.background == 0xfff6f7f9;
        }
        System.out.println("PASS " + scenario);
    }
}

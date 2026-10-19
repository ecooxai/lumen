package org.lumen.browser;

import android.app.Activity;
import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.util.SparseArray;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.RelativeLayout;
import android.widget.TextView;

/* The toolbar row over the page: the address field, the recent action slots and the more menu.
 * It is an Android view, so it draws and answers touches on the UI thread even while the engine
 * thread is busy with a page; the native side sends its state (android_ui_toolbar) and gets the
 * choices back as UI actions. */
final class BrowserToolbar extends FrameLayout {
    private static final int BAR = 0xfffafafa, LINE = 0xffe2e2e2, FIELD = 0xffeeeeee, FIELD_EDIT = 0xffffffff,
        TEXT = 0xff3c3c3c, ADDRESS = 0xff5a5a5a, EDIT_TEXT = 0xff141414, DISABLED = 0xffbebebe,
        ACCENT = 0xff1a73e8, PROGRESS = 0xff4285f4, BUSY = 0xffd93025;
    private static final String DOTS = "O 12 6 0.9 O 12 12 0.9 O 12 18 0.9";
    /* an on toggle carries a check inside its outline */
    private static final String DESKTOP_CHECK = " M8.8 10.6 L11 12.8 L15.2 8.4", FULLSCREEN_CHECK = " M8.5 12.2 L11 14.7 L15.7 9.6";
    private static final int SLOTS = 4;

    private final Activity activity;
    private final BrowserMenus.Action action;
    private final Runnable pageFocus;
    private final SparseArray<String> icons = new SparseArray<>();
    private final LinearLayout row;
    private final EditText field;
    private final GradientDrawable fieldShape = new GradientDrawable();
    private final ImageView[] slots = new ImageView[SLOTS];
    private final ImageView more;
    private final View progress;
    /* the CPU readout (Settings): the phone's use over Lumen's, and its details when tapped */
    private final LinearLayout meter;
    private final TextView systemCpu, browserCpu;
    private final BrowserStats stats = new BrowserStats(this::showStats);
    private PopupWindow detail;
    private TextView[] detailValues;
    private boolean hud, paused;
    /* android_ui_toolbar's numbers: geometry, loading, slots, then the more menu's state */
    private int[] state;
    private String address = "";
    /* the address field is being edited: it keeps the focus until stopEditing */
    private volatile boolean holding;
    private float unit = -1;

    BrowserToolbar(Activity activity, BrowserMenus.Action action, Runnable pageFocus) {
        super(activity);
        this.activity = activity;
        this.action = action;
        this.pageFocus = pageFocus;
        Context context = activity;
        setBackgroundColor(BAR);
        setClickable(true);
        setVisibility(GONE);

        row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        addView(row, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        field = new EditText(context) {
            @Override public boolean onKeyPreIme(int keyCode, KeyEvent event) {
                if (keyCode == KeyEvent.KEYCODE_BACK && event.getAction() == KeyEvent.ACTION_UP && hasFocus()) { stopEditing(); return true; }
                return super.onKeyPreIme(keyCode, event);
            }
        };
        field.setSingleLine(true);
        field.setHorizontallyScrolling(true);
        field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        field.setImeOptions(EditorInfo.IME_ACTION_GO | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        field.setSelectAllOnFocus(true);
        field.setCursorVisible(false);
        field.setTextColor(ADDRESS);
        field.setGravity(Gravity.CENTER_VERTICAL);
        field.setBackground(fieldShape);
        field.setContentDescription("Address");
        field.setOnFocusChangeListener((view, focused) -> { if (focused && !holding) { holding = true; look(true); field.setText(address); field.selectAll(); } });
        field.setOnEditorActionListener((view, id, event) -> {
            boolean enter = event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER;
            if (id != EditorInfo.IME_ACTION_GO && !enter) return false;
            if (enter && event.getAction() != KeyEvent.ACTION_DOWN) return true;
            String typed = field.getText().toString().trim();
            if (!typed.isEmpty()) {
                action.send(BrowserMenus.URL_GO, 0, null, typed);
                if (!typed.regionMatches(true, 0, "javascript:", 0, 11)) address = typed;
            }
            stopEditing();
            return true;
        });
        fieldShape.setColor(FIELD);
        row.addView(field);

        meter = new LinearLayout(context);
        meter.setOrientation(LinearLayout.VERTICAL);
        meter.setGravity(Gravity.CENTER_VERTICAL | Gravity.END);
        systemCpu = meterLine(context, ADDRESS);
        browserCpu = meterLine(context, TEXT);
        meter.addView(systemCpu);
        meter.addView(browserCpu);
        meter.setContentDescription("CPU use");
        meter.setVisibility(GONE);
        TypedValue ripple = new TypedValue();
        if (context.getTheme().resolveAttribute(android.R.attr.selectableItemBackgroundBorderless, ripple, true)) meter.setBackgroundResource(ripple.resourceId);
        meter.setOnClickListener(view -> showDetail());
        row.addView(meter);

        for (int i = 0; i < SLOTS; i++) {
            final int index = i;
            slots[i] = button(context);
            slots[i].setOnClickListener(view -> slot(index));
            row.addView(slots[i]);
        }
        more = button(context);
        more.setContentDescription("More options");
        more.setOnClickListener(view -> {
            if (state == null) return;
            stopEditing();
            int[] menu = new int[9];
            System.arraycopy(state, 18, menu, 0, 9);
            BrowserMenus.showMenu(activity, menu, action);
        });
        row.addView(more);

        View line = new View(context);
        line.setBackgroundColor(LINE);
        addView(line, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 1, Gravity.BOTTOM));
        progress = new View(context);
        progress.setBackgroundColor(PROGRESS);
        progress.setVisibility(GONE);
        addView(progress, new FrameLayout.LayoutParams(0, 2, Gravity.BOTTOM | Gravity.START));
    }

    private static ImageView button(Context context) {
        ImageView view = new ImageView(context);
        view.setScaleType(ImageView.ScaleType.CENTER);
        TypedValue value = new TypedValue();
        if (context.getTheme().resolveAttribute(android.R.attr.selectableItemBackgroundBorderless, value, true)) view.setBackgroundResource(value.resourceId);
        return view;
    }

    private static TextView meterLine(Context context, int color) {
        TextView line = new TextView(context);
        line.setTextColor(color);
        line.setSingleLine(true);
        line.setIncludeFontPadding(false);
        line.setGravity(Gravity.END);
        line.setText("\u2013");
        return line;
    }

    private static String percent(float value) { return value < 0 ? "\u2013" : Math.round(value) + "%"; }
    /* the meter's: two digits at least (07%), so its numbers line up and keep their width */
    private static String meterPercent(float value) { return value < 0 ? "\u2013" : String.format(java.util.Locale.ROOT, "%02d%%", Math.round(value)); }
    private static String megabytes(double bytes) { return Math.round(bytes / 1048576) + " MB"; }

    /* new numbers from the sampler, on the UI thread */
    private void showStats(BrowserStats s) {
        systemCpu.setText(meterPercent(s.system));
        browserCpu.setText(meterPercent(s.browser));
        systemCpu.setTextColor(s.system >= 80 ? BUSY : ADDRESS);
        browserCpu.setTextColor(s.browser >= 100 ? BUSY : TEXT);
        meter.setContentDescription("Phone CPU " + percent(s.system) + ", Lumen CPU " + percent(s.browser));
        if (detail == null || !detail.isShowing()) return;
        String[] values = {
            s.system < 0 ? "\u2013" : percent(s.system) + " of " + s.cores + (s.cores == 1 ? " core" : " cores"),
            s.browser < 0 ? "\u2013" : percent(s.browser) + " of one core",
            percent(s.tab) + " of one core",
            megabytes(s.resident),
            megabytes(s.heap * 1048576.0) + " + " + megabytes(s.external * 1048576.0) + " external",
            s.memoryTotal <= 0 ? "\u2013" : String.format(java.util.Locale.US, "%.1f of %.1f GB used",
                (s.memoryTotal - s.memoryAvailable) / 1073741824.0, s.memoryTotal / 1073741824.0),
            String.valueOf(s.tabs),
        };
        for (int i = 0; i < values.length; i++) detailValues[i].setText(values[i]);
    }

    private void showDetail() {
        if (detail != null && detail.isShowing()) { detail.dismiss(); return; }
        Context context = getContext();
        float density = context.getResources().getDisplayMetrics().density;
        int pad = Math.round(16 * density);
        LinearLayout box = new LinearLayout(context);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, Math.round(12 * density), pad, Math.round(12 * density));
        TextView title = new TextView(context);
        title.setText("CPU and memory");
        title.setTextColor(EDIT_TEXT);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        title.setPadding(0, 0, 0, Math.round(6 * density));
        box.addView(title);
        String[] labels = { "Phone CPU", "Lumen CPU", "Current tab", "Lumen memory", "JS heap, all tabs", "Phone memory", "Tabs" };
        detailValues = new TextView[labels.length];
        for (int i = 0; i < labels.length; i++) {
            LinearLayout line = new LinearLayout(context);
            line.setPadding(0, Math.round(4 * density), 0, Math.round(4 * density));
            TextView name = new TextView(context);
            name.setText(labels[i]);
            name.setTextColor(ADDRESS);
            name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
            line.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            detailValues[i] = new TextView(context);
            detailValues[i].setTextColor(EDIT_TEXT);
            detailValues[i].setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
            detailValues[i].setGravity(Gravity.END);
            detailValues[i].setPadding(Math.round(12 * density), 0, 0, 0);
            line.addView(detailValues[i]);
            box.addView(line);
        }
        TextView note = new TextView(context);
        note.setText("Updated every 3 seconds. Settings can hide this.");
        note.setTextColor(ADDRESS);
        note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        note.setPadding(0, Math.round(6 * density), 0, 0);
        box.addView(note);
        GradientDrawable background = new GradientDrawable();
        background.setColor(0xffffffff);
        background.setCornerRadius(10 * density);
        detail = new PopupWindow(box, Math.round(300 * density), ViewGroup.LayoutParams.WRAP_CONTENT, true);
        detail.setBackgroundDrawable(background);
        detail.setElevation(10 * density);
        detail.setOutsideTouchable(true);
        TouchFeedback.popup(detail);
        showStats(stats);
        detail.showAsDropDown(meter, 0, 0, Gravity.END);
    }

    /* the activity is paused or resumed: no sampling while it is not seen */
    void pause(boolean pause) {
        paused = pause;
        stats.run(hud && !paused);
    }

    private InputMethodManager ime() { return (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE); }

    /* the address as shown when not editing: without its scheme */
    private static String shown(String url) {
        int scheme = url.indexOf("://");
        return scheme >= 0 ? url.substring(scheme + 3) : url;
    }

    private void look(boolean edit) {
        field.setCursorVisible(edit);
        field.setTextColor(edit ? EDIT_TEXT : ADDRESS);
        fieldShape.setColor(edit ? FIELD_EDIT : FIELD);
        fieldShape.setStroke(edit ? Math.max(1, Math.round(1.5f * unit)) : 0, ACCENT);
    }

    boolean editing() { return holding; }

    /* leaves the address field: the keyboard goes and the page has the focus again */
    void stopEditing() {
        if (!holding) return;
        holding = false;
        look(false);
        field.setText(shown(address));
        ime().hideSoftInputFromWindow(field.getWindowToken(), 0);
        pageFocus.run();
        if (field.hasFocus()) field.clearFocus();
    }

    /* SDL moves the focus to its own views when the page's text input starts or stops; while the
       address is being edited the field takes it back */
    void focusMoved(View now) {
        if (!holding || now == field) return;
        field.post(() -> {
            if (!holding || field.hasFocus()) return;
            field.requestFocus();
            ime().showSoftInput(field, 0);
        });
    }

    /* true when a touch at window coordinates is outside the toolbar */
    boolean outside(MotionEvent event) {
        int[] at = new int[2];
        getLocationInWindow(at);
        float x = event.getX() - at[0], y = event.getY() - at[1];
        return getVisibility() != VISIBLE || x < 0 || y < 0 || x >= getWidth() || y >= getHeight();
    }

    private void slot(int index) {
        if (state == null || index >= state[5]) return;
        int base = 6 + 3 * index, code = state[base];
        if (state[base + 1] == 0) return;
        stopEditing();
        boolean lit = state[base + 2] != 0;
        int value = 0;
        /* the toggles show their new state at once; the native side confirms it with its next update */
        if (code == BrowserMenus.FULLSCREEN) {
            value = lit ? 0 : 1;
            BrowserInsets.fullscreen(activity.getWindow(), value != 0);
            flip(base, 18 + 8, value);
        } else if (code == BrowserMenus.DESKTOP) {
            value = lit ? 0 : 1;
            if (state[18 + 2] != 0) flip(base, 18 + 1, value);
        } else if (code == BrowserMenus.BM_TOGGLE && state[18 + 6] != 0) {
            flip(base, 18 + 5, lit ? 0 : 1);
        }
        action.send(code, value, null, null);
    }

    private void flip(int base, int menuIndex, int on) {
        state[base + 2] = on;
        state[menuIndex] = on;
        icons(state[5]);
    }

    private String icon(int code) {
        String source = icons.get(code);
        if (source == null) {
            try { source = LumenActivity.nativeUiIcon(code); } catch (UnsatisfiedLinkError error) { source = null; }
            if (source == null) source = "";
            icons.put(code, source);
        }
        return source;
    }

    private void icons(int count) {
        int size = Math.round(20 * unit);
        for (int i = 0; i < SLOTS; i++) {
            slots[i].setVisibility(i < count ? VISIBLE : GONE);
            if (i >= count) continue;
            int base = 6 + 3 * i, code = state[base];
            boolean enabled = state[base + 1] != 0, lit = state[base + 2] != 0;
            String source = icon(code);
            if (lit && code == BrowserMenus.DESKTOP) source += DESKTOP_CHECK;
            if (lit && code == BrowserMenus.FULLSCREEN) source += FULLSCREEN_CHECK;
            slots[i].setImageDrawable(source.isEmpty() ? null : BrowserIcons.path(source, !enabled ? DISABLED : lit ? ACCENT : TEXT, size));
            slots[i].setEnabled(enabled);
            slots[i].setContentDescription(BrowserMenus.actionName(code, lit));
        }
    }

    /* a new state from the native side: see android_ui_toolbar */
    void update(int[] next, String url) {
        if (next == null || next.length < 27) return;
        state = next;
        if (next.length >= 29) {
            TouchFeedback.enabled = next[28] != 0;
            if (next.length >= 33) TouchFeedback.style(next[29], next[30], next[31], next[32]);
            if (hud != (next[27] != 0)) {
                hud = next[27] != 0;
                meter.setVisibility(hud ? VISIBLE : GONE);
                if (!hud && detail != null) detail.dismiss();
                stats.run(hud && !paused);
            }
        }
        int width = next[2], height = next[3];
        RelativeLayout.LayoutParams at = getLayoutParams() instanceof RelativeLayout.LayoutParams
            ? (RelativeLayout.LayoutParams) getLayoutParams() : new RelativeLayout.LayoutParams(width, height);
        if (at.leftMargin != next[0] || at.topMargin != next[1] || at.width != width || at.height != height) {
            at.leftMargin = next[0];
            at.topMargin = next[1];
            at.width = width;
            at.height = height;
            setLayoutParams(at);
        }
        float u = height / 44f;
        if (u != unit) {
            unit = u;
            int button = Math.round(44 * u);
            LinearLayout.LayoutParams fieldAt = new LinearLayout.LayoutParams(0, height - 2 * Math.round(7 * u), 1);
            fieldAt.leftMargin = Math.round(10 * u);
            fieldAt.rightMargin = Math.round(2 * u);
            field.setLayoutParams(fieldAt);
            field.setTextSize(TypedValue.COMPLEX_UNIT_PX, 14 * u);
            field.setPadding(Math.round(16 * u), 0, Math.round(16 * u), 0);
            fieldShape.setCornerRadius((height - 2 * Math.round(7 * u)) / 2f);
            for (ImageView slot : slots) slot.setLayoutParams(new LinearLayout.LayoutParams(button, button));
            more.setLayoutParams(new LinearLayout.LayoutParams(button, button));
            LinearLayout.LayoutParams meterAt = new LinearLayout.LayoutParams(Math.round(36 * u), button);
            meterAt.leftMargin = Math.round(2 * u);
            meter.setLayoutParams(meterAt);
            meter.setPadding(0, 0, Math.round(4 * u), 0);
            systemCpu.setTextSize(TypedValue.COMPLEX_UNIT_PX, 11 * u);
            browserCpu.setTextSize(TypedValue.COMPLEX_UNIT_PX, 11 * u);
            more.setImageDrawable(BrowserIcons.path(DOTS, TEXT, Math.round(20 * u)));
            ((FrameLayout.LayoutParams) progress.getLayoutParams()).height = Math.max(2, Math.round(2 * u));
        }
        FrameLayout.LayoutParams bar = (FrameLayout.LayoutParams) progress.getLayoutParams();
        bar.width = Math.round(width * 0.35f);
        progress.setLayoutParams(bar);
        progress.setVisibility(next[4] != 0 ? VISIBLE : GONE);
        icons(next[5]);
        if (url == null) url = "";
        if (!url.equals(address)) {
            address = url;
            if (!field.hasFocus()) field.setText(shown(address));
        }
        setVisibility(VISIBLE);
    }
}

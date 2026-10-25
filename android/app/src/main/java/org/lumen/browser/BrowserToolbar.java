package org.lumen.browser;

import android.view.MenuItem;
import android.view.Menu;
import android.view.ActionMode;
import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.drawable.GradientDrawable;
import android.os.SystemClock;
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
        ACCENT = 0xff1a73e8, PROGRESS = 0xff4285f4, BUSY = 0xffd93025, DOT = 0xff2d2d30;
    private static final String DOTS = "O 12 6 0.9 O 12 12 0.9 O 12 18 0.9";
    /* an on toggle carries a check inside its outline */
    private static final String DESKTOP_CHECK = " M8.8 10.6 L11 12.8 L15.2 8.4", FULLSCREEN_CHECK = " M8.5 12.2 L11 14.7 L15.7 9.6";
    private static final int SLOTS = 4;

    private final Activity activity;
    private final BrowserMenus.Action action;
    private final Runnable pageFocus;
    private final SparseArray<String> icons = new SparseArray<>();
    private final LinearLayout row;
    /* the rounded address box: the site icon (site info when tapped, a magnifier while editing), then the field */
    private final LinearLayout pill;
    private final ImageView site;
    private int siteShown = -1;
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
    /* the touch indicator at the field's right end: 0 none, 1-4 an arrow up/down/left/right, 5 a dot */
    private int indicator;
    private static final int IND_MS = 1000, IND_TRAVEL = 6;   /* hold time; finger travel in dp that sets or turns the arrow */
    private boolean indTouch;
    private int indPointer, indDir;
    private float indX, indY;
    private long indDownT = -1000000, indDirT = -1000000;
    private final Runnable indExpire = this::indicatorRefresh;
    private final Paint indicatorPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path arrow = new Path();

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
            @Override protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                if (indicator != 0 && !holding && unit > 0) drawIndicator(canvas, getScrollX() + getWidth() - 14 * unit, getHeight() / 2f);
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
        field.setBackground(null);
        field.setContentDescription("Address");
        field.setOnFocusChangeListener((view, focused) -> {
            if (focused && !holding) { holding = true; look(true); field.setText(address); field.selectAll(); }
            if (focused) showFieldBar();
        });
        field.setOnClickListener(view -> { if (holding) showFieldBar(); });
        ActionMode.Callback none = new ActionMode.Callback() {   /* Lumen's own bar replaces the system text toolbar */
            @Override public boolean onCreateActionMode(ActionMode mode, Menu menu) { return false; }
            @Override public boolean onPrepareActionMode(ActionMode mode, Menu menu) { return false; }
            @Override public boolean onActionItemClicked(ActionMode mode, MenuItem item) { return false; }
            @Override public void onDestroyActionMode(ActionMode mode) { }
        };
        field.setCustomSelectionActionModeCallback(none);
        field.setCustomInsertionActionModeCallback(none);
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
        pill = new LinearLayout(context);
        pill.setOrientation(LinearLayout.HORIZONTAL);
        pill.setGravity(Gravity.CENTER_VERTICAL);
        pill.setBackground(fieldShape);
        site = button(context);
        site.setOnClickListener(view -> {
            if (holding || state == null) return;
            BrowserMenus.showSiteInfo(activity, pill, address, action);
        });
        pill.addView(site);
        pill.addView(field, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
        row.addView(pill);

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
            slots[i].setOnLongClickListener(view -> slotHeld(index));
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
        browserCpu.setTextColor(s.browser >= 50 ? BUSY : TEXT);
        meter.setContentDescription("Phone CPU " + percent(s.system) + ", Lumen CPU " + percent(s.browser));
        if (detail == null || !detail.isShowing()) return;
        String[] values = {
            s.system < 0 ? "\u2013" : percent(s.system) + " of " + s.cores + (s.cores == 1 ? " core" : " cores"),
            s.browser < 0 ? "\u2013" : percent(s.browser) + " of all cores",
            percent(s.tab) + " of all cores",
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

    /* the site icon: a lock on https, a warning on http, an info sign on other pages, a magnifier while editing */
    private void siteIcon() {
        int kind = holding ? -2 : BrowserMenus.siteKind(address);
        if (kind == siteShown || unit <= 0) return;
        siteShown = kind;
        site.setVisibility(kind == BrowserMenus.SITE_NONE ? GONE : VISIBLE);
        site.setClickable(kind >= 0);
        String source = kind == -2 ? BrowserIcons.SEARCH : BrowserMenus.siteIcon(kind);
        site.setImageDrawable(BrowserIcons.path(source, kind == BrowserMenus.SITE_INSECURE ? BUSY : ADDRESS, Math.round(17 * unit)));
        site.setContentDescription(kind == -2 ? null : kind == BrowserMenus.SITE_SECURE ? "Connection is secure, site information"
            : kind == BrowserMenus.SITE_INSECURE ? "Not secure, site information" : "Site information");
        field.setPadding(Math.round((kind == BrowserMenus.SITE_NONE ? 16 : 2) * unit), 0, Math.round(28 * unit), 0);   /* the text stops short of the indicator */
    }

    private void look(boolean edit) {
        siteIcon();
        field.setCursorVisible(edit);
        field.setTextColor(edit ? EDIT_TEXT : ADDRESS);
        fieldShape.setColor(edit ? FIELD_EDIT : FIELD);
        fieldShape.setStroke(edit ? Math.max(1, Math.round(1.5f * unit)) : 0, ACCENT);
    }

    boolean editing() { return holding; }

    /* leaves the address field: the keyboard goes and the page has the focus again */
    private void showFieldBar() {
        post(() -> { if (holding && field.hasFocus()) BrowserMenus.showFieldBar(this, field, () -> field.onEditorAction(EditorInfo.IME_ACTION_GO), action); });
    }

    void stopEditing() {
        BrowserMenus.hideFieldBar();
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

    /* a long press on the Bookmarks slot adds the current page; other slots keep the default */
    private boolean slotHeld(int index) {
        if (state == null || index >= state[5] || state[6 + 3 * index] != BrowserMenus.BOOKMARKS) return false;
        stopEditing();
        if (BrowserMenus.bookmarkHere(slots[index], state[18 + 5] != 0, state[18 + 6] != 0, action)) {
            state[18 + 5] = 1;
            for (int i = 0; i < state[5]; i++) if (state[6 + 3 * i] == BrowserMenus.BM_TOGGLE) state[6 + 3 * i + 2] = 1;
            icons(state[5]);
        }
        return true;
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

    /* Every window touch, on the UI thread, before the page sees it: the indicator never waits for the
       native loop, so a page busy loading or running script does not delay it. A touch that starts on
       the page shows a dark dot while the finger is down (at least 1 s), and a blue arrow the way the
       finger moves (held 1 s after the last move). A second finger is ignored. */
    void indicatorTouch(MotionEvent event) {
        long now = SystemClock.uptimeMillis();
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            indTouch = outside(event);
            if (!indTouch) return;
            indPointer = event.getPointerId(0); indDownT = now; indDir = 0; indX = event.getX(); indY = event.getY();
        } else if (!indTouch) return;
        else if (action == MotionEvent.ACTION_MOVE) {
            int at = event.findPointerIndex(indPointer);
            if (at < 0) return;
            float dx = event.getX(at) - indX, dy = event.getY(at) - indY, travel = IND_TRAVEL * getResources().getDisplayMetrics().density;
            if (Math.abs(dx) < travel && Math.abs(dy) < travel) return;
            indDir = Math.abs(dy) >= Math.abs(dx) ? (dy < 0 ? 1 : 2) : dx < 0 ? 3 : 4;
            indDirT = now; indX = event.getX(at); indY = event.getY(at);
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL
                || action == MotionEvent.ACTION_POINTER_UP && event.getPointerId(event.getActionIndex()) == indPointer) indTouch = false;
        else return;
        indicatorRefresh();
    }

    /* shows the indicator's state now and wakes up again at its next change */
    private void indicatorRefresh() {
        long now = SystemClock.uptimeMillis(), next = -1;
        int kind;
        if (indDir != 0 && now - indDirT < IND_MS) { kind = indDir; next = indDirT + IND_MS; }
        else if (indTouch) kind = 5;
        else if (now - indDownT < IND_MS) { kind = 5; next = indDownT + IND_MS; }
        else kind = 0;
        removeCallbacks(indExpire);
        if (next >= 0) postDelayed(indExpire, next - now + 1);
        if (kind == indicator) return;
        indicator = kind;
        field.invalidate();
    }

    /* a dark dot while a finger is down, else a blue arrow the way the finger moves, centred on (cx,cy) */
    private void drawIndicator(Canvas canvas, float cx, float cy) {
        float u = unit;
        indicatorPaint.setStyle(Paint.Style.FILL);
        if (indicator == 5) {
            indicatorPaint.setColor(DOT);
            canvas.drawCircle(cx, cy, 4 * u, indicatorPaint);
            return;
        }
        indicatorPaint.setColor(ACCENT);
        arrow.reset();   /* pointing up, then turned */
        arrow.moveTo(0, -6.5f); arrow.lineTo(5.5f, -0.5f); arrow.lineTo(1.6f, -0.5f); arrow.lineTo(1.6f, 6.5f);
        arrow.lineTo(-1.6f, 6.5f); arrow.lineTo(-1.6f, -0.5f); arrow.lineTo(-5.5f, -0.5f); arrow.close();
        canvas.save();
        canvas.translate(cx, cy);
        canvas.scale(u, u);
        canvas.rotate(indicator == 2 ? 180 : indicator == 3 ? -90 : indicator == 4 ? 90 : 0);
        canvas.drawPath(arrow, indicatorPaint);
        canvas.restore();
    }

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
        if (width <= 0 || height <= 0) {   /* the Downloads viewer's full screen: no toolbar */
            if (field.hasFocus()) field.clearFocus();
            setVisibility(GONE);
            return;
        }
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
            pill.setLayoutParams(fieldAt);
            site.setLayoutParams(new LinearLayout.LayoutParams(Math.round(34 * u), ViewGroup.LayoutParams.MATCH_PARENT));
            site.setPadding(Math.round(4 * u), 0, 0, 0);
            field.setTextSize(TypedValue.COMPLEX_UNIT_PX, 14 * u);
            siteShown = -1;
            fieldShape.setCornerRadius((height - 2 * Math.round(7 * u)) / 2f);
            for (ImageView slot : slots) slot.setLayoutParams(new LinearLayout.LayoutParams(button, button));
            more.setLayoutParams(new LinearLayout.LayoutParams(button, button));
            LinearLayout.LayoutParams meterAt = new LinearLayout.LayoutParams(Math.round(23 * u), button);   /* a quarter smaller than the 30 it was */
            meterAt.leftMargin = Math.round(2 * u);
            meter.setLayoutParams(meterAt);
            meter.setPadding(0, 0, Math.round(4 * u), 0);
            systemCpu.setTextSize(TypedValue.COMPLEX_UNIT_PX, 6.75f * u);
            browserCpu.setTextSize(TypedValue.COMPLEX_UNIT_PX, 6.75f * u);
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
        siteIcon();
        setVisibility(VISIBLE);
    }
}
